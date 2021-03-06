//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Extract functions from an input-file
//   and put them into the function-collection (not yet parsed)
//
//******************************************************************************

#include "stdafx.h"
#include "CFormulaFileSplitter.h"

#include "CFunction.h"
#include "CFunctionCollection.h"
#include "COHScriptObject.h"
#include "COHScriptList.h"
#include "CParseErrors.h"
#include "OH_MessageBox.h"

// Format of a formula file:
// * Date and time (OH-script only)
//     ##2014-02-09 23:16:55##
// * Notes (OH-script only)
//     ##Notes##
//     Comments below
// * DLL
//     ##dll##
//     Filename
// * Lists
//     ##list_preflop_monsters##
//     AA KK ...  
// * formulas
//     ##f$alli##
//     C-style expressions or Shanky-style WHEN-conditions
//     Somewhat special cases: f$test and f$debug

const CString kErroneousFunctionName = "-- undefined --";

// public
CFormulaFileSplitter::CFormulaFileSplitter() {
}

// public
CFormulaFileSplitter::~CFormulaFileSplitter() {
}

// public
void CFormulaFileSplitter::SplitFile(CArchive &formula_file) {
  _first_function_processed = false;
  _total_lines_processed = 0;
  _starting_line_of_next_function = 0;
  COHScriptObject* next_function_or_list = GetNextObject(formula_file);
  while (next_function_or_list != NULL) {
    p_function_collection->Add(next_function_or_list);
    next_function_or_list = GetNextObject(formula_file);
  }
}

COHScriptObject* CFormulaFileSplitter::GetNextObject(CArchive &formula_file) {
  // We have to get the starting line of the current-function
  // (= end of last found function) before we scan for its content,
  // which already modifies the line-info of the next one.
  int starting_line_of_current_function = _starting_line_of_next_function;
  ScanForNextFunctionOrList(formula_file);
  if (_function_name == kErroneousFunctionName) {
    // Input really mal-formed.
    // Shanky-PPL, old-style OpenPPL or something else.
    // We already threw a warning.
    // Returning NULL will terminate parsing (of this file),
    // but this easy solution is OK here.
    return NULL;
  }
  COHScriptObject* new_function_or_list = NULL;
  if (_function_name.Left(4) == "list") {
    new_function_or_list = new COHScriptList(
      _function_name, 
      _function_text, 
      formula_file.GetFile()->GetFilePath(),
      starting_line_of_current_function);
  } else {
    new_function_or_list = new CFunction(
      _function_name,
      _function_text,
      formula_file.GetFile()->GetFilePath(),
      starting_line_of_current_function);
  }
  return new_function_or_list;
}

CString CFormulaFileSplitter::ExtractFunctionName(const CString function_header) {
  // Check correct start of function header 
  // and especially throw warnings on old-style and Shanky-style PPL.
  // Most common error: identifier instead of function header
  // This can only happen before the first function
  CString function_name_lower_case = function_header;
  function_name_lower_case.MakeLower();
  if (function_name_lower_case.Left(6) == "custom") {
    CParseErrors::Error("Superfluous keyword custom.\n"
      "OpenPPL 7.0 is somewhat different than old-style OpenPPL and Shanky-PPL.\n"
      "Please have a look at the manual for all the differences.\n"
      "It really matters!\n");
    return kErroneousFunctionName;
  } 
  else if ((function_name_lower_case.Left(7) == "preflop")
    || (function_name_lower_case.Left(4) == "flop")
    || (function_name_lower_case.Left(4) == "turn")
    || (function_name_lower_case.Left(5) == "river")) {
    CParseErrors::Error("Shanky-style betrounds.\n"
      "OpenHoldem-style function expected.\n"
      "Example: ##f$preflop##\n");
    return kErroneousFunctionName;
  }
  else if (function_name_lower_case.Left(3) == "new") {
    CParseErrors::Error("Old-style OpenPPL function.\n"
      "OpenHoldem-style ##f$function## expected.\n");
    return kErroneousFunctionName;
  }
  else if ((function_name_lower_case.Left(2) == "//")
    || (function_name_lower_case.Left(2) == "/*")) {
    CParseErrors::Error("Top-level comment outside function.\n"
      "Technically a formula-file is a set of functions\n"
      "and every comment belongs to such a function.\n"
      "A top-level comment outside of a function would get lost.\n"
      "Please put it for example into \"##notes##\".\n");
    return kErroneousFunctionName;
  }
  else if (function_name_lower_case == "") {
    // End of file reached;
    // no more functions found
    return kErroneousFunctionName;
  }
  else if (function_name_lower_case.Left(2) != "##") {
    CParseErrors::Error("Shanky-style option settings?\n"
      "Options are not supported, because OpenPPL does not provide a default bot.\n"
      "They need to be removed.\n"
      "Expecting a ##f$function## or ##listXYZ## instead.\n");
    return kErroneousFunctionName;
  }
  // Leading ## found
  if (function_name_lower_case.Right(2) != "##") {
    CParseErrors::Error("Malformed function-header. Trailing ## expected.\n");
    return kErroneousFunctionName;
  }
  // New header verified
  CString function_name = function_header;
  // Get rid pf ## at both ends
  int length = function_name.GetLength();
  assert(length >= 4);
  function_name = function_name.Left(length - 2);
  function_name = function_name.Right(length - 4);
  if (function_name.GetLength() <= 0) {
    CParseErrors::Error("Empty function or list name.\n"
      "Expecting a ##f$function## here.\n");
    return kErroneousFunctionName;
  }
  return function_name;
}




// Returns the next function (including header),
// i.e. everything up to the second-next-function or end of file.
void CFormulaFileSplitter::ScanForNextFunctionOrList(CArchive &formula_file) {
  // Function-header is the first line, 
  // (usually the last line of last scan)
  // rest is content
  CString function_header = _next_line;
  _function_text = ""; 
  while (true) {
    if (!formula_file.ReadString(_next_line)) {
	    break;
    }
    ++_total_lines_processed;
    if (_total_lines_processed == 1) {
      SanityChecksForWrongFileTypes();
    }
    // Avoid problems with "empty" lines before first function header
    // that  contain spaces.
    _next_line.TrimRight();
#ifdef DEBUG_FORMULA_FILESPLITTER
    //printf("[CFormulaFileSplitter] next line: %s\n", _next_line);
#endif
    if (IsFunctionHeader(_next_line)
        && _first_function_processed) {
      // Next function found
      // Only continue, if we found the first one
      //
      // In case of break: keep that function-header for the next query
      _starting_line_of_next_function = _total_lines_processed;
      break;
	  }
    if (function_header.IsEmpty() || (function_header.Find('#') < 0)) {
      // Escpecially meant to catch OpenGeeks newlines 
      // (which are not empty) at the beginning of the file.
      // Other cases can't happen, as we search for ## 
      // when looking for the next function-header. 
      function_header = _next_line;
    } else {
      _first_function_processed = true;
      // Add non-function-header (content) to the functions body
      _function_text += _next_line;
      _function_text += "\n";
    }
  } 
  _function_name = ExtractFunctionName(function_header);
  // Remove superfluous new-lines at the end of the function
  // to avoid blowing up the files on saving...
  _function_text.TrimRight();
  // except one, that is necessary for the tokenizer
  // and results in better formatting
  _function_text += "\n";
#ifdef DEBUG_FORMULA_FILESPLITTER
  //printf("[CFormulaFileSplitter] next function: %s\n", _formula_content);
  OH_MessageBox_Interactive(_function_text, "Function", 0);
#endif
}



// A function header (or list header) starts with ##
bool CFormulaFileSplitter::IsFunctionHeader(CString line_of_code)
{
	return ((line_of_code.GetAt(0) == '#')
		&& (line_of_code.GetAt(1) == '#'));
}

void CFormulaFileSplitter::SanityChecksForWrongFileTypes() {
  // Some people manage to feed OpenHoldem with unexpected file-types, including
  //  * Downloaded web-pages
  //  * tablemaps
  // We try to diagnose such PEBKACs here
  CString first_line = _next_line;
  first_line.MakeLower();
  if ((first_line.Left(4) == "<htm")
    || (first_line.Left(4) == "<!do")
    || (first_line.Left(4) == "<xml")) {
    // http://www.maxinmontreal.com/forums/viewtopic.php?f=297&t=19814&p=139309
    CParseErrors::Error("Invalid file-type.\n"
      "The input looks like HTML or XML.\n"
      "Did you download a demo from the internet\n"
      "and save a web-page instead of plain text?\n");
  }
  else if (first_line.Left(5) == ".osdb") {
    // http://www.maxinmontreal.com/forums/viewtopic.php?f=117&t=20046#p140821
    CParseErrors::Error("Invalid file-type.\n"
      "The input looks like a tablemap.\n"
      "Tablemaps have to be put into the scraper-directory\n"
      "and then get loaded automatically.\n"
      "Menu->File->Open is only for bot-logic.\n");
  }
}
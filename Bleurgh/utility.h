//
//  utility.h
//  Bleurgh
//
//  Created by Andrew Bennett on 5/08/13.
//  Copyright (c) 2013 TeamBnut. All rights reserved.
//

#ifndef __Bleurgh__utility__
#define __Bleurgh__utility__

#include <cstddef>
#include <string>

typedef struct _pc_state
{
    size_t gpos, lpos, line;
} pc_state;

typedef struct _parse_context parse_context;

parse_context * pc_create(const char * filename);
void pc_destroy(parse_context * parser);

bool pc_skip_whitespace(parse_context * parser);

bool pc_match_char(parse_context * parser, char c);
bool pc_match_string(parse_context * parser, const char * str);
bool pc_match_identifier(parse_context * parser, std::string * str_out);

bool pc_extract_FP(parse_context * parser, double * value);

pc_state pc_save(parse_context * parser);
void pc_load(parse_context * parser, pc_state state);

int  pc_error(parse_context * parser, const char * message);

#endif /* defined(__Bleurgh__utility__) */

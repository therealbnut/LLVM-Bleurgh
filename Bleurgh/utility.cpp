//
//  utility.cpp
//  Bleurgh
//
//  Created by Andrew Bennett on 5/08/13.
//  Copyright (c) 2013 TeamBnut. All rights reserved.
//

#include "utility.h"

#include <string>
#include <fstream>
#include <iostream>

struct _parse_context
{
    std::string filename;
    std::ifstream stream;
    std::ios::pos_type line_start;
    size_t line_number;
};

parse_context * pc_create(const char * filename)
{
    parse_context * parser = new _parse_context();
    parser->stream.open(filename);
    if (!parser->stream.good())
    {
        delete parser;
        return NULL;
    }
    parser->filename    = filename;
    parser->line_start  = parser->stream.tellg();
    parser->line_number = 1;
    return parser;
}

void pc_destroy(parse_context * parser)
{
    delete parser;
}

pc_state pc_save(parse_context * parser)
{
    pc_state state;
    state.line = parser->line_number;
    state.lpos = parser->line_start;
    state.gpos = parser->stream.tellg();
    return state;
}
void pc_load(parse_context * parser, pc_state state)
{
    parser->line_number = state.line;
    parser->line_start  = state.lpos;
    parser->stream.seekg(state.gpos);
}

int pc_error(parse_context * parser, const char * message)
{
    // %s:%d:%d: error: %s
    std::cerr << parser->filename << ':';
    std::cerr << parser->line_number << ':';
    std::cerr << (parser->stream.tellg()-parser->line_start+1) << ": error: ";
    std::cerr << message << std::endl;
    return EXIT_FAILURE;
}

bool pc_skip_whitespace(parse_context * parser)
{
    int c;
    c = parser->stream.peek();
    if (isspace(c))
    {
        do
        {
            parser->stream.ignore();
            if (c == '\n')
            {
                parser->line_start = parser->stream.tellg();
                ++parser->line_number;
            }
            c = parser->stream.peek();
        }
        while (isspace(c));
        return true;
    }
    return false;
}
bool pc_match_char(parse_context * parser, char c)
{
    pc_state state = pc_save(parser);
    if (parser->stream.get() != c)
    {
        pc_load(parser, state);
        return false;
    }
    return true;
}
bool pc_match_string(parse_context * parser, const char * str)
{
    pc_state state = pc_save(parser);
    for (int i=0; str[i]; ++i)
    {
        if (parser->stream.get() != str[i])
        {
            pc_load(parser, state);
            return false;
        }
    }
    return true;
}
bool pc_match_identifier(parse_context * parser, std::string * str_out)
{
    pc_state state = pc_save(parser);
    *str_out = "";
    for (;;)
    {
        char c = parser->stream.peek();
        if (isalnum(c) || c=='_')
        {
            *str_out += parser->stream.get();
            continue;
        }
        if (str_out->empty())
        {
            pc_load(parser, state);
            return false;
        }
        return true;
    }
}

bool pc_extract_FP(parse_context * parser, double * value)
{
    if (!(parser->stream >> *value))
    {
        parser->stream.clear();
        return false;
    }
    return true;
}


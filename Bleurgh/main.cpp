//
//  main.cpp
//  Bleurgh
//
//  Created by Andrew Bennett on 5/08/13.
//  Copyright (c) 2013 TeamBnut. All rights reserved.
//

#include "utility.h"

#include <iostream>
#include <fstream>

#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/Scalar.h"

const char * kEntryPoint = "bleurgh_main";

typedef struct _bl_scope bl_scope;

bl_scope * bl_scope_create(void);
void bl_scope_destroy(bl_scope * scope);
void bl_scope_push(bl_scope * scope);
void bl_scope_pop(bl_scope * scope);

void bl_scope_save(bl_scope * scope, const char * name, llvm::Value * value);
llvm::Value* bl_scope_load(bl_scope * scope, const char * name);

int bl_binop_precedence(char c)
{
    switch (c)
    {
        case '+': return 2000;
        case '-': return 2000;
        case '*': return 4000;
        case '/': return 4000;
    }
    return 0;
}

llvm::Value * bl_parse_const_FP(parse_context * parser)
{
    double const_value = 0;
    if (pc_extract_FP(parser, &const_value))
    {
        return llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(const_value));
    }
    return NULL;
}

llvm::Value * bl_parse_expression(parse_context * parser, bl_scope * scope, llvm::IRBuilder<>& builder);

bool bl_parse_param_array(parse_context * parser, std::vector<std::string> * param_out)
{
    pc_state state = pc_save(parser);
    std::string param;
    param_out->clear();

    if (!pc_match_string(parser, "("))
    {
        pc_error(parser, "Expecting '('!");
        pc_load(parser, state);
        return false;
    }

    pc_skip_whitespace(parser);
    while (pc_match_identifier(parser, &param))
    {
        param_out->push_back(param);
        pc_skip_whitespace(parser);
    }

    if (!pc_match_string(parser, ")"))
    {
        pc_error(parser, "Expecting ')'!");
        pc_load(parser, state);
        return false;
    }

    return true;
}

bool bl_extract_function_declaration(parse_context * parser, bl_scope * scope, llvm::Module * module,
                                     std::string * func_name_out)
{
    pc_state state = pc_save(parser);

    pc_skip_whitespace(parser);
    if (!pc_match_string(parser, "function"))
    {
        pc_load(parser, state);
        return false;
    }

    std::string ident;
    pc_skip_whitespace(parser);
    if (!pc_match_identifier(parser, &ident))
    {
        pc_error(parser, "Expecting identifier!");
        pc_load(parser, state);
        return false;
    }
    
    std::vector<std::string> params;
    pc_skip_whitespace(parser);
    if (!bl_parse_param_array(parser, &params))
    {
        pc_error(parser, "Expecting parameter array!");
        pc_load(parser, state);
        return false;
    }

    llvm::Type * double_type = llvm::Type::getDoubleTy(module->getContext());
    std::vector<llvm::Type*> param_types(params.size(), double_type);

    llvm::Function * old_func = llvm::cast_or_null<llvm::Function>(bl_scope_load(scope, ident.c_str()));
    if (old_func != NULL)
    {
        if (double_type != old_func->getReturnType())
        {
            pc_error(parser, "Function return type mismatch!");
            pc_load(parser, state);
            return false;
        }
        if (old_func->arg_size() != params.size())
        {
            pc_error(parser, "Parameter count mismatch!");
            pc_load(parser, state);
            return false;
        }
        llvm::Function::const_arg_iterator i, e;
        for (i=old_func->arg_begin(), e=old_func->arg_end(); i!=e; ++i)
        {
            if (i->getType() != double_type)
            {
                pc_error(parser, "Parameter type mismatch!");
                pc_load(parser, state);
                return false;
            }
        }
    }
    else
    {
        llvm::FunctionType * entry_type  = llvm::FunctionType::get(double_type, param_types, false);
        llvm::Function *     definition  = llvm::Function::Create(entry_type,
                                                                  llvm::Function::ExternalLinkage,
                                                                  ident, module);
        bl_scope_save(scope, ident.c_str(), definition);
    }

    if (func_name_out != NULL)
    {
        *func_name_out = ident;
    }

    return true;
}

bool bl_parse_block_internal(parse_context * parser, bl_scope * scope, llvm::IRBuilder<>& builder)
{
    llvm::Value * ret_value = bl_parse_expression(parser, scope, builder);
    if (ret_value == NULL)
    {
        return false;
    }
    
    builder.CreateRet(ret_value);

    return true;
}

llvm::Value * bl_parse_function_definition(parse_context * parser, bl_scope * scope, llvm::Module * module)
{
    pc_state state = pc_save(parser);
    std::string func_name;
    if (!bl_extract_function_declaration(parser, scope, module, &func_name))
    {
        return NULL;
    }
    llvm::Function * func = llvm::cast_or_null<llvm::Function>(bl_scope_load(scope, func_name.c_str()));
    if (!func)
    {
        pc_error(parser, "Unexpected compiler error!");
        pc_load(parser, state);
        return NULL;
    }

    pc_skip_whitespace(parser);
    if (!pc_match_string(parser, "{"))
    {
        pc_load(parser, state);
        return NULL;
    }

    if (!func->getBasicBlockList().empty())
    {
        pc_error(parser, "Redefining function!");
        pc_load(parser, state);
        return NULL;
    }

    llvm::LLVMContext& context = module->getContext();
    llvm::IRBuilder<>  builder(context);
    llvm::BasicBlock * block = llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(block);

    if (!bl_parse_block_internal(parser, scope, builder))
    {
        func->deleteBody();
        pc_load(parser, state);
        return false;
    }

    pc_skip_whitespace(parser);
    if (!pc_match_string(parser, "}"))
    {
        func->deleteBody();
        pc_error(parser, "Expecting '}' at end of function definition!");
        pc_load(parser, state);
        return false;
    }

    llvm::verifyFunction(*func);

    return func;
}

llvm::Value * bl_parse_par_expr(parse_context * parser, bl_scope * scope, llvm::IRBuilder<>& builder)
{
    pc_state state = pc_save(parser);
    pc_skip_whitespace(parser);
    if (!pc_match_string(parser, "("))
    {
        pc_load(parser, state);
        return NULL;
    }

    llvm::Value * val = bl_parse_expression(parser, scope, builder);
    if (val == NULL)
    {
        pc_error(parser, "Expecting expression in brackets!");
        pc_load(parser, state);
        return NULL;
    }

    pc_skip_whitespace(parser);
    if (!pc_match_string(parser, ")"))
    {
        pc_error(parser, "Expecting ')'!");
        pc_load(parser, state);
        return NULL;
    }

    return val;
}

llvm::Value * bl_parse_primary(parse_context * parser, bl_scope * scope, llvm::IRBuilder<>& builder)
{
    llvm::Value * value;
    pc_skip_whitespace(parser);
    if ((value = bl_parse_const_FP(parser)) != NULL) return value;
    if ((value = bl_parse_par_expr(parser, scope, builder)) != NULL) return value;
    return NULL;
}

char bl_extract_binop(parse_context * parser)
{
    const char opcode_chars[] = "+-*/";
    int opcode;
    pc_skip_whitespace(parser);
    for (opcode=0; opcode_chars[opcode]; ++opcode)
    {
        if (pc_match_char(parser, opcode_chars[opcode]))
            break;
    }
    
    return opcode_chars[opcode];
}

llvm::Value * bl_parse_binop_rhs(parse_context * parser,
                                 bl_scope * scope, 
                                 llvm::IRBuilder<>& builder,
                                 int expr_prec, llvm::Value * lhs)
{
    pc_state initial_state = pc_save(parser);
    for (;;)
    {
        pc_state state = pc_save(parser);
        char op_curr = bl_extract_binop(parser);
        int op_curr_prec = bl_binop_precedence(op_curr);
        if (!op_curr || op_curr_prec < expr_prec)
        {
            pc_load(parser, state);
            return lhs;
        }

        llvm::Value * rhs = bl_parse_primary(parser, scope, builder);
        if (!rhs)
        {
            pc_load(parser, state);
            return NULL;
        }

        state = pc_save(parser);
        char op_next = bl_extract_binop(parser);
        int op_next_prec = bl_binop_precedence(op_next);

        if (!op_next || op_curr_prec < op_next_prec)
        {
            pc_load(parser, state);
            rhs = bl_parse_binop_rhs(parser, scope, builder, op_curr_prec+1, rhs);
            if (rhs == NULL)
            {
                pc_load(parser, initial_state);
                return NULL;
            }
        }
        else
        {
            pc_load(parser, state);
        }

        switch (op_curr)
        {
            case '+': lhs = builder.CreateFAdd(lhs, rhs); break;
            case '-': lhs = builder.CreateFSub(lhs, rhs); break;
            case '*': lhs = builder.CreateFMul(lhs, rhs); break;
            case '/': lhs = builder.CreateFDiv(lhs, rhs); break;
            default:
                pc_error(parser, "Unexpected operator");
                std::cerr << "error: Unexpected operator: '" << op_curr << "'" << std::endl;
                abort();
        }
    }
}

llvm::Value * bl_parse_expression(parse_context * parser, bl_scope * scope, llvm::IRBuilder<>& builder)
{
    llvm::Value * lhs;
    lhs = bl_parse_primary(parser, scope, builder);
    if (lhs == NULL)
    {
        pc_error(parser, "Expecting expression!");
        return NULL;
    }
    return bl_parse_binop_rhs(parser, scope, builder, 0, lhs);
}

llvm::Module * bl_parse_from_file(parse_context * parser, bl_scope * scope, llvm::LLVMContext& context)
{
    std::string errmsg;;
    llvm::Module * module = new llvm::Module("Bleurgh JIT", context);
    llvm::ExecutionEngine * ex_engine = llvm::EngineBuilder(module).setErrorStr(&errmsg).create();
    if (!ex_engine)
    {
        std::cerr << "error: Could not create ExecutionEngine, " << errmsg << std::endl;
        exit(EXIT_FAILURE);
    }
    
    if (bl_parse_function_definition(parser, scope, module))
    {
        printf("extracted definition\n");
    }
    else if (bl_extract_function_declaration(parser, scope, module, NULL))
    {
        printf("extracted declaration\n");
    }
    else
    {
        exit(EXIT_FAILURE);
    }

    return module;
}

void call_main(llvm::Module * module)
{
    std::string errmsg;
    llvm::ExecutionEngine * ex_engine = llvm::EngineBuilder(module).setErrorStr(&errmsg).create();
    if (!ex_engine)
    {
        std::cerr << "error: Could not create ExecutionEngine, " << errmsg << std::endl;
        exit(EXIT_FAILURE);
    }
    llvm::Function * entry = module->getFunction(kEntryPoint);
    double (*entry_callback)(void) = (double (*)(void))(intptr_t) ex_engine->getPointerToFunction(entry);
    std::cout << "Output: " << entry_callback() << "\n" << std::endl;
}

void output_object(const char * filename, llvm::Module * module)
{
    std::string errmsg;
    const llvm::Target * target;
    llvm::TargetOptions options;
    const char * cpu_name = "";
    const char * features = "";
    llvm::Triple triple;
    llvm::TargetMachine * machine;

    triple = llvm::Triple(module->getTargetTriple());
    if (triple.getTriple().empty())
        triple.setTriple(llvm::sys::getDefaultTargetTriple());
    
    target = llvm::TargetRegistry::lookupTarget(triple.getTriple(), errmsg);

    machine = target->createTargetMachine(triple.getTriple(),
                                          cpu_name, features, options,
                                          llvm::Reloc::Default,
                                          llvm::CodeModel::Default,
                                          llvm::CodeGenOpt::Default);

    llvm::tool_output_file output(filename, errmsg);
    if (!errmsg.empty())
    {
        std::cerr << "error: Unable to open '" << filename << "' for output (" << errmsg << ")!" << std::endl;
        exit(EXIT_FAILURE);
    }

    llvm::PassManager passes;
    machine->addAnalysisPasses(passes);
    passes.add(new llvm::DataLayout(*machine->getDataLayout()));


    machine->setAsmVerbosityDefault(true);
    machine->setMCRelaxAll(true);

    llvm::formatted_raw_ostream out_stream(output.os());
    if (machine->addPassesToEmitFile(passes, out_stream, llvm::TargetMachine::CGFT_ObjectFile))
    {
        std::cerr << "error: " << filename << ": Target does not support generation of this file type!" << std::endl;
        exit(EXIT_FAILURE);
    }
    output.keep();

    passes.run(*module);
}

int main(int argc, const char * argv[])
{
    std::string errmsg;
    
    if (argc < 2)
    {
        std::cerr << "Usage: Bleurgh <input> [output]" << std::endl;
        exit(EXIT_FAILURE);
    }

    llvm::LLVMContext& context = llvm::getGlobalContext();
    if (argc < 3)
    {
        llvm::InitializeNativeTarget();
    }
    else
    {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        llvm::PassRegistry * pass_reg = llvm::PassRegistry::getPassRegistry();
        llvm::initializeCore(*pass_reg);
        llvm::initializeCodeGen(*pass_reg);
        llvm::initializeLoopStrengthReducePass(*pass_reg);
        llvm::initializeLowerIntrinsicsPass(*pass_reg);
        llvm::initializeUnreachableBlockElimPass(*pass_reg);
    }

    parse_context * file = pc_create(argv[1]);
    if (file == NULL)
    {
        std::cerr << "error: Unable to open '" << argv[1] << "' for input!" << std::endl;
        exit(EXIT_FAILURE);
    }

    bl_scope * scope = bl_scope_create();
    llvm::Module * module = bl_parse_from_file(file, scope, context);
    pc_destroy(file);
    bl_scope_destroy(scope);

    if (argc < 3)
    {
        module->dump();
        call_main(module);
    }
    else
    {
        output_object(argv[2], module);
    }

    exit(EXIT_SUCCESS);
}

#include <vector>
#include <map>

struct _bl_scope
{
    std::vector<std::map<std::string,llvm::Value*> > stack;
};

bl_scope * bl_scope_create(void)
{
    bl_scope * scope = new bl_scope;
    scope->stack.push_back(std::map<std::string,llvm::Value*>());
    return scope;
}

void bl_scope_destroy(bl_scope * scope)
{
    delete scope;
}

void bl_scope_push(bl_scope * scope)
{
    scope->stack.push_back(scope->stack.back());
}
void bl_scope_pop(bl_scope * scope)
{
    scope->stack.erase(scope->stack.end()-1);
}

void bl_scope_save(bl_scope * scope, const char * name, llvm::Value * value)
{
    std::map<std::string,llvm::Value*>& top = scope->stack.back();
    top[name] = value;
}
llvm::Value* bl_scope_load(bl_scope * scope, const char * name)
{
//#error need to store map in something mutable?
    std::vector<std::map<std::string,llvm::Value*> >::reverse_iterator i;
    for (i=scope->stack.rbegin(); i!=scope->stack.rend(); ++i)
    {
        std::map<std::string,llvm::Value*>::iterator j;
        j = i->find(name);
        if (j != i->end())
        {
            return j->second;
        }
    }
    return NULL;
}


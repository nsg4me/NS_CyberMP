#pragma once

// Calls a script-visible game function through RTTI, matching arguments to parameters by NAME.
//
// Native engine methods are usually reached through hard-coded vtable slots, which move whenever a patch adds a
// virtual function (e.g. gameITransactionSystem shifted between 2.2x and 2.5). Script-visible functions are looked up
// by name at runtime instead, so they survive patches. Matching parameters by name (instead of position) also keeps
// working when a patch appends or reorders optional parameters.

namespace Game
{
struct NamedArg
{
    Red::CName name;
    Red::CBaseRTTIType* type;
    void* value;
};

template<typename T>
inline NamedArg Arg(Red::CName aName, T& aValue)
{
    return {aName, Red::ResolveType<std::remove_cvref_t<T>>(), const_cast<std::remove_cvref_t<T>*>(&aValue)};
}

// Returns false when the function is missing, a required parameter was not supplied, a type does not match, or the
// call itself failed. Parameters that are not supplied must be optional in the game's declaration.
template<typename TRet>
inline bool CallNamed(Red::IScriptable* aContext, Red::CName aFunction, TRet& aResult,
                      std::initializer_list<NamedArg> aArgs)
{
    if (!aContext)
        return false;

    auto* func = Red::Detail::GetFunction(aContext->GetType(), aFunction);
    if (!func)
    {
        spdlog::warn("[ScriptCall] {} not found", aFunction.ToString());
        return false;
    }

    Red::StackArgs_t args;
    args.reserve(func->params.Size());

    for (uint32_t i = 0; i < func->params.Size(); ++i)
    {
        const auto* param = func->params[i];

        const NamedArg* match = nullptr;
        for (const auto& arg : aArgs)
        {
            if (arg.name == param->name)
            {
                match = &arg;
                break;
            }
        }

        if (match)
        {
            if (!Red::IsCompatible(param->type, match->type))
            {
                spdlog::warn("[ScriptCall] {}: parameter {} has an unexpected type", aFunction.ToString(),
                             param->name.ToString());
                return false;
            }
            args.emplace_back(match->type, match->value);
        }
        else if (param->flags.isOptional)
        {
            args.emplace_back(param->type, nullptr);
        }
        else
        {
            spdlog::warn("[ScriptCall] {}: required parameter {} was not supplied", aFunction.ToString(),
                         param->name.ToString());
            return false;
        }
    }

    Red::CStack stack(aContext);
    stack.args = args.data();
    stack.argsCount = static_cast<uint32_t>(args.size());

    Red::CStackType result(Red::ResolveType<TRet>(), &aResult);
    if (func->returnType)
    {
        if (!Red::IsCompatible(result.type, func->returnType->type))
        {
            spdlog::warn("[ScriptCall] {}: unexpected return type", aFunction.ToString());
            return false;
        }
        stack.result = &result;
    }

    return Red::Detail::CallFunctionWithStack(func, stack);
}
} // namespace Game

#include "EmuVshShaderCreation.h"

#include <utility>

namespace XTL::VshShaderCreation
{
Plan BuildPlan(const Request& request)
{
    Plan plan;
    bool declarationCpuCompatible = true;
    std::string declarationCpuIncompatibilityReason;

    if(request.isXboxFunction && request.diagnosticSink == nullptr)
    {
        plan.disposition = Disposition::Reject;
        plan.reason = "missing_diagnostic_sink";
        return plan;
    }

    if(request.isXboxFunction)
    {
        const VshDiagnostics::XboxFunctionDisposition disposition =
            VshDiagnostics::ClassifyXboxFunction(request.xboxFunction, plan.reason);
        if(disposition == VshDiagnostics::XboxFunctionDisposition::Reject)
        {
            plan.disposition = Disposition::Reject;
            return plan;
        }
        if(disposition == VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu)
        {
            if(!request.hasDeclaration)
            {
                plan.disposition = Disposition::Reject;
                plan.reason = "cpu_fallback_requires_declaration";
                return plan;
            }
            plan.disposition = Disposition::ExecuteOnCpu;
        }
        else
        {
            VshDiagnostics::FunctionTranslationResult translation =
                VshDiagnostics::TranslateXboxFunction(request.xboxFunction,
                                                      request.diagnosticSink);
            plan.translatedFunction = std::move(translation.tokens);
            if(plan.translatedFunction.empty())
            {
                plan.disposition = Disposition::Reject;
                plan.reason = "recompilation_failed";
                return plan;
            }
        }
    }

    if(request.hasDeclaration)
    {
        const VshDiagnostics::DeclarationTranslationResult declarationResult =
            VshDiagnostics::TranslateXboxDeclaration(request.xboxDeclaration,
                                                     plan.translatedDeclaration);
        plan.declarationTokenCount = declarationResult.tokenCount;
        declarationCpuCompatible = declarationResult.cpuCompatible;
        plan.translatedDeclarationAvailable =
            declarationResult.disposition !=
                VshDiagnostics::XboxFunctionDisposition::Reject &&
            declarationResult.tokenCount != 0;
        declarationCpuIncompatibilityReason =
            declarationResult.cpuIncompatibilityReason;

        if(plan.disposition == Disposition::ExecuteOnCpu &&
           !declarationResult.cpuCompatible)
        {
            plan.disposition = Disposition::Reject;
            plan.reason = declarationResult.cpuIncompatibilityReason;
            return plan;
        }
        if(declarationResult.disposition ==
           VshDiagnostics::XboxFunctionDisposition::Reject)
        {
            plan.disposition = Disposition::Reject;
            plan.reason = declarationResult.reason;
            return plan;
        }
        if(declarationResult.disposition ==
           VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu)
        {
            if(!request.isXboxFunction)
            {
                plan.disposition = Disposition::Reject;
                plan.reason = "cpu_declaration_requires_xbox_function";
                return plan;
            }
            plan.disposition = Disposition::ExecuteOnCpu;
            plan.reason = declarationResult.reason;
            plan.translatedFunction.clear();
        }
    }

    if(plan.translatedFunction.empty())
    {
        return plan;
    }

    try
    {
        const VshDiagnostics::ValidationResult validation =
            VshDiagnostics::ValidateD3D8Translation(request.xboxFunction,
                                                    plan.translatedFunction);
        if(validation.valid)
        {
            return plan;
        }

        const bool exceedsHostLimit =
            validation.message == "instruction count exceeds the vs.1.1 limit of 128";
        if(exceedsHostLimit && request.hasDeclaration && declarationCpuCompatible)
        {
            plan.disposition = Disposition::ExecuteOnCpu;
            plan.reason = "host_instruction_limit";
            return plan;
        }

        plan.disposition = Disposition::Reject;
        plan.reason = exceedsHostLimit && request.hasDeclaration
                          ? declarationCpuIncompatibilityReason
                          : validation.message;
        return plan;
    }
    catch(...)
    {
        request.diagnosticSink("VshDecoder: validation raised a host exception");
        plan.disposition = Disposition::Reject;
        plan.reason = "translation_validation_exception";
        return plan;
    }
}
} // namespace XTL::VshShaderCreation

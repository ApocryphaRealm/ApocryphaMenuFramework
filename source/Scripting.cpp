#include "Scripting.h"

#include "Persistence.h"
#include "utils/Logger.h"

namespace scripting
{
	namespace
	{
		// AMF_Ping - the smallest possible round-trip proof: a Papyrus script calls this, the
		// native side logs it fired, returns a value the script can also log. Confirms
		// RegisterFunction actually bound and the VM actually dispatched into our DLL.
		std::int32_t AMF_Ping(RE::StaticFunctionTag*)
		{
			logger::info("scripting: AMF_Ping() called from Papyrus - native binding confirmed live");
			return 1;
		}

		// AMF_SetTestValue / AMF_GetTestValue - exercises the persistence channel (Persistence.h)
		// FROM Papyrus, proving a script author could use AMF's per-save state without touching
		// SKSE's serialization API themselves.
		void AMF_SetTestValue(RE::StaticFunctionTag*, RE::BSFixedString a_value)
		{
			persistence::SetValue("papyrus-test", a_value.c_str());
			logger::info("scripting: AMF_SetTestValue(\"{}\") called from Papyrus", a_value.c_str());
		}

		RE::BSFixedString AMF_GetTestValue(RE::StaticFunctionTag*)
		{
			const std::string value = persistence::GetValue("papyrus-test", "<unset>");
			logger::info("scripting: AMF_GetTestValue() called from Papyrus, returning \"{}\"", value);
			return RE::BSFixedString(value);
		}

		bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
		{
			if (!a_vm)
			{
				logger::error("scripting: RegisterFuncs called with a null VM; native functions NOT registered");
				return false;
			}

			a_vm->RegisterFunction("AMF_Ping", "AMFTest", AMF_Ping);
			a_vm->RegisterFunction("AMF_SetTestValue", "AMFTest", AMF_SetTestValue);
			a_vm->RegisterFunction("AMF_GetTestValue", "AMFTest", AMF_GetTestValue);

			logger::info("scripting: 3 native function(s) registered against class \"AMFTest\"");
			return true;
		}
	}

	void RegisterNativeFunctions()
	{
		if (!SKSE::GetPapyrusInterface()->Register(RegisterFuncs))
		{
			logger::error("scripting: SKSE Papyrus interface refused the registration callback");
		}
	}
}

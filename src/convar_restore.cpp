#include "convar_restore.h"
#include "mem/detour.h"

class CEmptyConVar : public ConVar {};


namespace ConVar_Restore
{
	ConCommand ccmd_save("sig_cvar_save", &Save, "Save the values of this mod's ConVars");
	ConCommand ccmd_savenondefault("sig_cvar_save_non_default", &SaveNonDefault, "Save the values of this mod's ConVars, excluding default values");
	ConCommand ccmd_load("sig_cvar_load", &Load, "Load the previously saved ConVar values of this mod");
	ConCommand ccmd_reset("sig_cvar_reset", &Reset, "Reset ConVars to default values");
	ConVar cvar_autosave("sig_cvar_autosave", "0", FCVAR_NONE, "Should ConVar values automatically be saved on disk during mod unload");
	
	std::list<ConVar *>     s_ConVars;
	std::list<ConCommand *> s_ConCmds;
	
	
	void Register(ConCommandBase *pCommand)
	{
		/* ignore s_EmptyConVar */
		if (dynamic_cast<CEmptyConVar *>(pCommand) != nullptr) return;
		
		if (pCommand->IsCommand()) {
		//	DevMsg("ConVar_Restore::Register: cmd \"%s\" @ %08x\n", cmd->GetName(), (uintptr_t)cmd);
			s_ConCmds.push_front(static_cast<ConCommand *>(pCommand));
		} else {
		//	DevMsg("ConVar_Restore::Register: var \"%s\" @ %08x\n", var->GetName(), (uintptr_t)var);
			s_ConVars.push_front(static_cast<ConVar *>(pCommand));
		}
	}
	
	void SaveActual(bool no_default, const char *path)
	{
	//	DevMsg("ConVar_Restore::Save\n");
		
		// Use text buffer instead of keyvalues for comments to be automatically generated

		CUtlBuffer file( 0, 0, CUtlBuffer::TEXT_BUFFER );

		// auto kv = new KeyValues("SigsegvConVars");
		// kv->UsesEscapeSequences(true);
		
		//file.PutString("SigsegvConVars\n{\n");

		file.PutString("\n//-------------\n//These are default values for convars added by sigsegv extension. Changed values should be added into sigsegv_convars.cfg file\n//-------------\n\n");
		for (auto var : s_ConVars) {
		//	DevMsg("  %s\n", var->GetName());
			
			// if (kv->FindKey(var->GetName()) != nullptr) {
			// 	Warning("ConVar_Restore::Save: ConVar \"%s\" is a duplicate\n", var->GetName());
			// 	continue;
			// }
			if (var->IsFlagSet(FCVAR_NEVER_AS_STRING)) {
				Warning("ConVar_Restore::Save: ConVar \"%s\" has unsupported flag FCVAR_NEVER_AS_STRING\n", var->GetName());
				continue;
			}
			
			if (no_default && strcmp(var->GetString(), var->GetDefault()) == 0) {
		//		DevMsg("    default: skip\n");
				continue;
			}
			
			file.PutString(var->GetName());
			file.PutString("	\"");
			file.PutString(var->GetString());
			file.PutChar('"');

			if (var->GetHelpText() != nullptr && *(var->GetHelpText())) {
				file.PutString("		// ");
				file.PutString(var->GetHelpText());
			}

			file.PutChar('\n');
			// kv->SetString(var->GetName(), var->GetString());
			
		//	auto subkey = new KeyValues(var->GetName());
		//	subkey->SetString("value", var->GetString());
		//	kv->AddSubKey(subkey);
		}

		//file.PutChar('}');
		if (!filesystem->WriteFile(path,"GAME", file)) {
			Warning("ConVar_Restore::Save: Could not save Ke27020yValues to \"cfg/sigsegv_convars.cfg\".\n");
		}
		
		//kv->deleteThis();
	}

	void Save()
	{
		SaveActual(false, "cfg/sigsegv_convars.cfg");
	}

	void SaveNonDefault()
	{
		SaveActual(true, "cfg/sigsegv_convars.cfg");
	}
	
	void Load()
	{
	//	DevMsg("ConVar_Restore::Load\n");
		
		engine->ServerCommand("exec sigsegv_convars.cfg\n");
		engine->ServerExecute();
		
		// auto kv = new KeyValues("SigsegvConVars");
		// kv->UsesEscapeSequences(true);
		
		// if (kv->LoadFromFile(filesystem, "cfg/sigsegv_convars.cfg")) {
		// 	FOR_EACH_VALUE(kv, subkey) {
		// 		const char *name  = subkey->GetName();
		// 		const char *value = subkey->GetString();
				
		// 		ConCommandBase *base = icvar->FindCommandBase(name);
		// 		if (base == nullptr) {
		// 			Warning("ConVar_Restore::Load: ConVar \"%s\" doesn't exist\n", name);
		// 			continue;
		// 		}
		// 		if (base->IsCommand()) {
		// 			Warning("ConVar_Restore::Load: ConVar \"%s\" is actually a ConCommand\n", name);
		// 			continue;
		// 		}
		// 		if (base->IsFlagSet(FCVAR_NEVER_AS_STRING)) {
		// 			Warning("ConVar_Restore::Load: ConVar \"%s\" has unsupported flag FCVAR_NEVER_AS_STRING\n", name);
		// 			continue;
		// 		}
				
		// 		auto var = static_cast<ConVar *>(base);
		// 		var->SetValue(value);
		// 	}
		// } else {
		// 	Warning("ConVar_Restore::Load: Could not load KeyValues from \"cfg/sigsegv_convars.cfg\".\n");
		// }
		
		// kv->deleteThis();
	}

	void Reset()
	{
		for (auto var : s_ConVars) {
			var->SetValue(var->GetDefault());
		}
		CDetouredFunc::CleanUp();
	}

	void OnExtLoad()
	{
		Load();

		if (!filesystem->FileExists("cfg/sigsegv_convars_default.cfg", "GAME")) {
			Msg("ConVar_Restore: Default ConVar values saved in cfg/sigsegv_convars_default.cfg\n");
			SaveActual(false, "cfg/sigsegv_convars_default.cfg");
		}
	}

	void OnExtUnload()
	{
		if (cvar_autosave.GetBool())
			SaveActual(false, "cfg/sigsegv_convars.cfg");
	}
}

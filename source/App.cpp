/*
 *  App.cpp
 *  Created by Seth Robinson on 3/6/09.
 *  For license info, check the license.txt file that should have come with this.
 *
 */
#include "PlatformPrecomp.h"
#include "App.h"
#include "GUI/MainMenu.h"
#include "Entity/EntityUtils.h"//create the classes that our globally library expects to exist somewhere.
#include "dink/dink.h"
#include "GUI/GameMenu.h"
#include "util/archive/TarHandler.h"
#include "Renderer/SoftSurface.h"
#include "GUI/BrowseMenu.h"
#include "Entity/SliderComponent.h"
#include "GUI/OptionsMenu.h"
#include "FileSystem/FileSystemZip.h"
#include "Entity/ArcadeInputComponent.h"
#include "GUI/ExpiredMenu.h"
#include <time.h>
#include "Gamepad/GamepadManager.h"
#include "Gamepad/GamepadProvideriCade.h"
#include "GUI/PopUpMenu.h"
#include "GUI/PauseMenu.h"

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#ifdef PLATFORM_HTML5
#include "html5/HTML5Utils.h"
#include "html5/SharedJSLIB.h"
#endif

extern bool g_script_debug_mode;
extern Surface g_transitionSurf;

MessageManager g_messageManager;
MessageManager * GetMessageManager() {return &g_messageManager;}

FileManager g_fileManager;
FileManager * GetFileManager() {return &g_fileManager;}

GamepadManager g_gamepadManager;
GamepadManager * GetGamepadManager() {return &g_gamepadManager;}

#include "Gamepad/GamepadProviderVita.h"
#include "Audio/AudioManagerSDL.h"
AudioManagerSDL g_audioManager;

AudioManager * GetAudioManager(){return &g_audioManager;}

App *g_pApp = NULL;
BaseApp * GetBaseApp() 
{
	if (!g_pApp)
	{
		g_pApp = new App;
	}

	return g_pApp;
}

App * GetApp() 
{
	return g_pApp;
}

const char * GetAppName()
{
	return "Dink Smallwood HD";
};


App::App()
{
	m_logFileHandle = NULL;

	m_bGhostMode = false;	
	m_bDidPostInit = false;
	m_bHasDMODSupport = true;
	//for mobiles
	m_version = 1.92f;
	m_versionString = "V1.92";
	m_build = 1;
	m_bCheatsEnabled = false;

	//for Win/mac
	m_desktopVersion = m_version;
	m_desktopVersionString = m_versionString; 
	m_desktopBuild = 1;
	m_bForceAspectRatio = true;
	
}

App::~App()
{

	assert(m_logFileHandle);
	if (m_logFileHandle)
		fclose(m_logFileHandle);
}


void App::AddIcadeProvider()
{
	GamepadProvider * pProv = GetGamepadManager()->AddProvider(new GamepadProvideriCade); //use iCade, this actually should work with any platform...
	GetBaseApp()->SetAllowScreenDimming(false);
	if (pProv)
	{
		pProv->m_sig_failed_to_connect.connect(1, boost::bind(&App::OniCadeDisconnected, this, _1));
	}
}

bool App::GetForceAspectRatio()
{
	return m_bForceAspectRatio;
}

bool App::UseClassicEscapeMenu()
{

	if (GetEmulatedPlatformID() == PLATFORM_ID_HTML5 && !GetApp()->GetUsingTouchScreen())
	{
		return true;
	}

	if (IsDesktop())
	{
		return true;
	}

	return false;
}

void App::OniCadeDisconnected(GamepadProvider *pProvider)
{
	LogMsg("Dealing with icade disconnect");
	GetGamepadManager()->RemoveProviderByName("iCade");
	GetApp()->RemoveAndroidKeyboardKeys();

	GetApp()->GetVar("check_icade")->Set(uint32(0));

	Entity *pOptions = GetEntityRoot()->GetEntityByName("OptionsMenu");
	if (pOptions)
	{
        LogMsg("Found options");
		Entity *pCheckBox = pOptions->GetEntityByName("check_icade");
		if (pCheckBox)
		{
            LogMsg("Found checkbox");
			SetCheckBoxChecked(pCheckBox, false, true);
		}
	}
}

bool App::DoesCommandLineParmExist(string parm)
{
	vector<string> parms = GetBaseApp()->GetCommandLineParms();
	parm = ToLowerCaseString(parm);
	for (int i = 0; i < parms.size(); i++)
	{
		if (ToLowerCaseString(parms[i]) == parm) return true;
	}
	return false;
}

bool App::Init()
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	
	SceNetInitParam netInitParam;
	int size = 4 * 1024 * 1024;
	netInitParam.memory = malloc(size);
	netInitParam.size = size;
	netInitParam.flags = 0;
	sceNetInit(&netInitParam);
	sceNetCtlInit();

#ifdef WINAPI
	InitUnhandledExceptionFilter();
#endif


	//GetBaseApp()->SetDisableSubPixelBlits(true);
	SetDefaultButtonStyle(Button2DComponent::BUTTON_STYLE_CLICK_ON_TOUCH_RELEASE);
	SetManualRotationMode(false);

	bool bScaleScreenActive = true;
	int scaleToX = 1024;
	int scaleToY = 768;

	SetLockedLandscape(false); //we don't allow portrait mode for this game
	SetupScreenInfo(GetPrimaryGLX(), GetPrimaryGLY(), ORIENTATION_PORTRAIT);
	if (bScaleScreenActive)
		SetupFakePrimaryScreenSize(scaleToX,scaleToY);


	//L_ParticleSystem::init(2000);
	SetInputMode(INPUT_MODE_SEPARATE_MOVE_TOUCHES); //this game has so much move touching, I handle them separately for performance reasons

	if (m_bInitted)	
	{
		return true;
	}
	
	if (!BaseApp::Init())
	{
		return false;
	}


	LogMsg("Initializing Dink HD %s", GetVersionString().c_str());
	LogMsg("Save path is %s", GetSavePath().c_str());

	//CreateAppCacheDirIfNeeded();

	
	if (IsLargeScreen())
	{
		if (!GetFont(FONT_SMALL)->Load("interface/font_normalx2.rtfont")) return false;
		if (!GetFont(FONT_LARGE)->Load("interface/font_bigx2.rtfont")) return false;
	}
	else
	{
		if (!GetFont(FONT_SMALL)->Load("interface/font_normal.rtfont")) return false;
		if (!GetFont(FONT_LARGE)->Load("interface/font_big.rtfont")) return false;
	}

	GetBaseApp()->SetFPSVisible(true);

	bool bFileExisted;
	m_varDB.Load("save.dat", &bFileExisted);
	
	GetApp()->GetVarWithDefault("smoothing",uint32(0))->GetUINT32();
	GetApp()->GetVarWithDefault("buttons",uint32(0));
	GetApp()->GetVarWithDefault("music_vol",1.0f)->GetFloat();
	GetApp()->GetVarWithDefault("gui_transparency",0.35f)->GetFloat();
	GetApp()->GetVarWithDefault("checkerboard_fix", uint32(1)); //default to on for Windows

	GetGamepadManager()->AddProvider(new GamepadProviderVita);

	if (GetVar("check_icade")->GetUINT32() != 0)
	{
		AddIcadeProvider();
	}

	bool hasPSTV = GetApp()->GetVarWithDefault("pstv_setting", Variant(uint32(0)))->GetUINT32() != 0;
	if (hasPSTV == true)
	{
		GetApp()->ToggleGamePadOrTouch(false);
	}
	else
	{
		GetApp()->ToggleGamePadOrTouch(true);
	}

	UpdateVideoSettings();
	GetApp()->SetCheatsEnabled(true);

	bool bSound = m_varDB.GetVarWithDefault("sound", uint32(1))->GetUINT32() != 0;
	GetAudioManager()->SetSoundEnabled(bSound);

	GetAudioManager()->SetMusicVol(GetApp()->GetVar("music_vol")->GetFloat());
	GetAudioManager()->Preload("audio/click.wav");
	InitDinkPaths(GetBaseAppPath(), "dink", "");
	

	GetBaseApp()->m_sig_pre_enterbackground.connect(1, boost::bind(&App::OnPreEnterBackground, this, _1));
	GetBaseApp()->m_sig_loadSurfaces.connect(1, boost::bind(&App::OnLoadSurfaces, this));
	GetBaseApp()->m_sig_unloadSurfaces.connect(1, boost::bind(&App::OnUnloadSurfaces, this));
	
	App::OnMemoryWarning();

	return true;
}

void App::OnPreEnterBackground(VariantList *pVList)
{
	SaveAllData();
}

void App::OnExitApp(VariantList *pVarList)
{
	LogMsg("Exiting the app");

	OSMessage o;
	o.m_type = OSMessage::MESSAGE_FINISH_APP;
	GetBaseApp()->AddOSMessage(o);
}

void App::Kill()
{
	sceNetCtlTerm();
	sceNetTerm();
	sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);

	if (!IsInBackground())
	{
		SaveAllData();
	}
	
	finiObjects();
	
	BaseApp::Kill();
	g_pApp = NULL; //make sure nobody elses access this
}


void App::RemoveAndAttachAllAvailableGamepads()
{
	ArcadeInputComponent *pComp = (ArcadeInputComponent*) GetEntityRoot()->GetComponentByName("ArcadeInput");
	assert(pComp);

	for (int i=0; i < GetGamepadManager()->GetGamepadCount(); i++)
	{
		Gamepad *pPad = GetGamepadManager()->GetGamepad((eGamepadID)i);
		pPad->ConnectToArcadeComponent(pComp, true, true);
	}
}

void App::RemoveAndroidKeyboardKeys()
{
	ArcadeInputComponent *pComp = (ArcadeInputComponent*) GetEntityRoot()->GetComponentByName("ArcadeInput");
	//first clear out all old ones to be safe
	VariantList vList((string)"Keyboard");
	pComp->GetFunction("RemoveKeyBindingsStartingWith")->sig_function(&vList);
}

void App::AddDroidKeyboardKeys()
{
}

void App::Update()
{
	BaseApp::Update();
	g_gamepadManager.Update();

	if (!m_bDidPostInit)
	{
		m_bDidPostInit = true;
		m_special = GetSystemData() != C_PIRATED_NO;
		
		Entity *pGUIEnt = GetEntityRoot()->AddEntity(new Entity("GUI"));
		ArcadeInputComponent *pComp = (ArcadeInputComponent*) GetEntityRoot()->AddComponent(new ArcadeInputComponent);
		
		RemoveAndAttachAllAvailableGamepads();

		//add key bindings, I may want to move these later if I add a custom key config...

		AddKeyBinding(pComp, "Left", VIRTUAL_KEY_DIR_LEFT, VIRTUAL_KEY_DIR_LEFT);
		AddKeyBinding(pComp, "Right", VIRTUAL_KEY_DIR_RIGHT, VIRTUAL_KEY_DIR_RIGHT);
		AddKeyBinding(pComp, "Up", VIRTUAL_KEY_DIR_UP, VIRTUAL_KEY_DIR_UP);
		AddKeyBinding(pComp, "Down", VIRTUAL_KEY_DIR_DOWN, VIRTUAL_KEY_DIR_DOWN);
		AddKeyBinding(pComp, "Talk", ' ', VIRTUAL_KEY_GAME_TALK);

		AddKeyBinding(pComp, "GamePadInventory", VIRTUAL_DPAD_SELECT, VIRTUAL_KEY_GAME_INVENTORY);
		AddKeyBinding(pComp, "GamePadInventory2", VIRTUAL_DPAD_BUTTON_UP, VIRTUAL_KEY_GAME_INVENTORY);
		AddKeyBinding(pComp, "GamePadEscape", VIRTUAL_DPAD_START, VIRTUAL_KEY_BACK, true);
		AddKeyBinding(pComp, "GamePadFire", VIRTUAL_DPAD_BUTTON_DOWN, VIRTUAL_KEY_GAME_FIRE);
		AddKeyBinding(pComp, "GamePadTalk", VIRTUAL_DPAD_BUTTON_RIGHT, VIRTUAL_KEY_GAME_TALK);
		AddKeyBinding(pComp, "GamePadMagic", VIRTUAL_DPAD_BUTTON_LEFT, VIRTUAL_KEY_GAME_MAGIC);
		
		AddKeyBinding(pComp, "GamePadSpeedup", VIRTUAL_DPAD_LBUTTON, 'M', true);
		AddKeyBinding(pComp, "GamePadSpeedup2", VIRTUAL_DPAD_RBUTTON, 9);
	
		AddKeyBinding(pComp, "GamePadInventory3", VIRTUAL_DPAD_LTRIGGER, VIRTUAL_KEY_GAME_INVENTORY);
		AddKeyBinding(pComp, "GamePadPause", VIRTUAL_DPAD_RTRIGGER, VIRTUAL_KEY_BACK, true);

		MainMenuCreate(pGUIEnt);
	}
	else
	{
		CheckForHotkeys();
	}
}

void App::Draw()
{
	BaseApp::Draw();
}

void App::OnScreenSizeChange()
{
	BaseApp::OnScreenSizeChange();
	if (GetPrimaryGLX() != 0)
	{
		SetupOrtho();
		DinkOnForeground(); //rebuild lost surfaces
		g_dglo.m_bForceControlsRebuild = true;
		if (GetDinkGameState() != DINK_GAME_STATE_PLAYING)
		{
			PrepareForGL();
		}

	}
}

void App::GetServerInfo( string &server, uint32 &port )
{
	server = "rtsoft.com";
	port = 80;
}

int App::GetSpecial()
{
	return m_special; //1 means pirated copy
}

Variant * App::GetVar( const string &keyName )
{
	return GetShared()->GetVar(keyName);
}

std::string App::GetVersionString()
{
	if (IsDesktop()) return m_desktopVersionString;
	return m_versionString;
}

float App::GetVersion()
{
	if (IsDesktop()) return m_desktopVersion;
	return m_version;
}

int App::GetBuild()
{
	if (IsDesktop()) return m_desktopBuild;
	return m_build;
}

void App::OnMemoryWarning()
{
	GetAudioManager()->KillCachedSounds(false, true, 0, 1, false);
	DinkUnloadUnusedGraphicsByUsageTime(100); //unload anything not used in the last second

	BaseApp::OnMemoryWarning();
}

void App::UpdateVideoSettings()
{
	eVideoFPS v = (eVideoFPS)GetApp()->GetVarWithDefault("fpsLimit", Variant(uint32(VIDEO_FPS_LIMIT_OFF)))->GetUINT32();
	SetFPSLimit(60);
};

void App::SaveSettings()
{
	m_varDB.Save("save.dat");
}

void App::SaveAllData()
{

	if (GetDinkGameState() == DINK_GAME_STATE_PLAYING)
	{
		SaveState(g_dglo.m_savePath+"continue_state.dat", false);
		WriteLastPathSaved(g_dglo.m_savePath);
	}
	SaveSettings();
}

void App::OnEnterBackground()
{
	BaseApp::OnEnterBackground();
}

void App::OnEnterForeground()
{
	if (GetPrimaryGLX() == 0) return;

	BaseApp::OnEnterForeground();
    
}

bool App::GetIconsOnLeft()
{
	return GetShared()->GetVar("buttons")->GetUINT32() != 0;
}

#ifdef _WINDOWS_
#include "win/app/main.h"
#endif



bool App::OnPreInitVideo()
{
	if (!BaseApp::OnPreInitVideo()) return false;
	return true;
}

const char * GetBundlePrefix()
{

	char * bundlePrefix = "com.rtsoft.";
	return bundlePrefix;
}

const char * GetBundleName()
{
	char * bundleName = "rtdink";
	return bundleName;
}


void ImportSaveState(string physicalFname)
{

	Entity *pMenu = GetEntityRoot()->GetEntityByName("GameMenu");

	string originalDMODDir = g_dglo.m_dmodGameDir;
	string newDMODDir;

	bool bSuccess = GetDMODDirFromState(physicalFname, newDMODDir);

	if (!bSuccess)
	{
		GetAudioManager()->Play("audio/buzzer2.wav");
		PopUpCreate(pMenu, "Error loading save state.  Probably an older version, sorry.", "", "cancel", "Continue", "", "", true);
	}
	else
	{
		LogMsg("We are in %s, but now we need %s", originalDMODDir.c_str(), newDMODDir.c_str());

		if (!newDMODDir.empty())
		{

			if (!FileExists(GetDMODRootPath() + newDMODDir + "dmod.diz"))
			{
				PopUpCreate(pMenu, "Can't find file " + newDMODDir + "dmod.diz" + ", maybe you need to install the DMOD first?", "", "cancel", "Continue", "", "", true);
				return;
			}
		}

		if (originalDMODDir == newDMODDir)
		{
			LoadStateWithExtra(physicalFname);
		}
		else
		{
			//whole different dmod, we need to get fancy here
			LogMsg("Switching to correct DMOD dir for this quicksave...");
			//SetDinkGameState(DINK_GAME_STATE_NOT_PLAYING);

			Entity *pNewMenu = DinkQuitGame();
			KillEntity(pMenu);

			DisableAllButtonsEntity(pNewMenu);
			SlideScreen(pNewMenu, false);
			GetMessageManager()->CallEntityFunction(pNewMenu, 500, "OnDelete", NULL);

			InitDinkPaths(GetBaseAppPath(), "dink", RemoveTrailingBackslash(newDMODDir));
			GameCreate(pNewMenu->GetParent(), 0, physicalFname);
			GetBaseApp()->SetGameTickPause(false);
		}
	}
}

void ImportNormalSaveSlot(string fileName, string outputFileName)
{
	//well... directly loading it is possible but.. uhh.. DMODs may need their own startup
	//scripts so I better just copy the file over instead.
	Entity *pMenu = GetEntityRoot()->GetEntityByName("GameMenu");

	string path = g_dglo.m_dmodGamePathWithDir;

	if (path.empty())
	{
		//must be dink and not a DMOD, special case
		path = GetSavePath()+"/dink/";
	}

	//fix outputfilename if it's wrong
	StripWhiteSpace(outputFileName);
	int index = outputFileName.find_first_of('(');
	if (index != string::npos)
	{
		//it probably looks like "save2 (2).dat" due to chrome renaming if it existed, fix it
		outputFileName = outputFileName.substr(0, index - 1) + "."+GetFileExtension(outputFileName);
	}
	LogMsg("Copying %s to %s", fileName.c_str(), (path + outputFileName).c_str());

	if (!GetFileManager()->Copy(fileName, path + outputFileName, false))
	{

		PopUpCreate(pMenu, ("Error copying " + outputFileName + " into "+ g_dglo.m_dmodGamePathWithDir + outputFileName+"!").c_str(), "", "cancel", "Continue", "", "", true);

		return;
	}
	
	PopUpCreate(pMenu, "Ok, we've put " + outputFileName +" into the active game directory. You can now load this save slot like normal.", "", "cancel", "Continue", "", "", true);

}


void App::OnMessage( Message &m )
{
	m_adManager.OnMessage(m); //gives the AdManager a way to handle messages
	
	
	if (m.GetClass() == MESSAGE_CLASS_GUI)
	{
		if (m.GetType() == MESSAGE_TYPE_HTML5_GOT_UPLOAD)
		{
			string fName = m.GetStringParm();
			string physicalFname = "proton_temp.tmp";
			int fileSize = GetFileSize(physicalFname);
			LogMsg("Got uploaded file %s (as %s). %d bytes", m.GetStringParm().c_str(), physicalFname.c_str(),
				fileSize);
			

			if (fileSize > 1024 * 1000)
			{
				//well, it's big, let's assume it's a full save state
				ImportSaveState(physicalFname);
			}
			else
			{
				ImportNormalSaveSlot(physicalFname, fName);

			}


		}
	}

	BaseApp::OnMessage(m);
}


void App::OnLoadSurfaces()
{
	LogMsg("Reloading dink engine surfaces");
	DinkOnForeground();

}

void App::OnUnloadSurfaces()
{
	LogMsg("Unloading dink engine surfaces");
	DinkUnloadUnusedGraphicsByUsageTime(0);
	
	//g_transitionSurf.Kill();
}

void App::ToggleGamePadOrTouch(bool chk)
{
	m_bUsingTouchScreen = chk;
}

void App::AddTextToLog(const char *tex, const char *filename)
	{
		if (strlen(tex) < 1) return;

		if (m_logFileHandle == NULL)
		{

			//open 'er up
			m_logFileHandle = fopen(filename, "wb");
			if (!m_logFileHandle)
			{
				assert(!"huh?");
			}
			return;
		}
		
		if (!m_logFileHandle) return;
			fwrite(tex, strlen(tex), 1, m_logFileHandle);
	
	}

#ifdef WINAPI
//our custom LogMsg that isn't slow as shit
void LogMsg(const char* traceStr, ...)
{
	va_list argsVA;
	const int logSize = 1024 * 10;
	char buffer[logSize];
	memset((void*)buffer, 0, logSize);

	va_start(argsVA, traceStr);
	vsnprintf_s(buffer, logSize, logSize, traceStr, argsVA);
	va_end(argsVA);


	OutputDebugString(buffer);
	OutputDebugString("\n");

	if (IsBaseAppInitted())
	{
		GetBaseApp()->GetConsole()->AddLine(buffer);
		strcat(buffer, "\r\n");
		//OutputDebugString( (string("writing to ")+GetSavePath()+"log.txt\n").c_str());
		//this is the slow part.  Or was...
		GetApp()->AddTextToLog(buffer, (GetSavePath() + "log.txt").c_str());
	}

}

#endif

#ifdef PLATFORM_OSX


//our custom LogMsg that isn't slow as shit
void LogMsg(const char* traceStr, ...)
{
    va_list argsVA;
    const int logSize = 1024 * 10;
    char buffer[logSize];
    memset((void*)buffer, 0, logSize);
    
    va_start(argsVA, traceStr);
    vsnprintf(buffer, logSize, traceStr, argsVA);
    va_end(argsVA);
    
    
    if (IsBaseAppInitted())
    {
        GetBaseApp()->GetConsole()->AddLine(buffer);
        strcat(buffer, "\r\n");
        //OutputDebugString( (string("writing to ")+GetSavePath()+"log.txt\n").c_str());
        //this is the slow part.  Or was...
        GetApp()->AddTextToLog(buffer, (GetSavePath() + "log.txt").c_str());
    }
    
}


#endif


bool TouchesHaveBeenReceived()
{

#ifdef _DEBUG
	//return true;
#endif

#ifdef PLATFORM_HTML5
	if (GetTouchesReceived() > 0) return true;
#endif
	return false;
}
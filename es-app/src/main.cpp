//EmulationStation, a graphical front-end for ROM browsing. Created by Alec "Aloshi" Lofquist.
//http://www.aloshi.com

#include <SDL.h>
#include <iostream>
#include <iomanip>
#include "Renderer.h"
#include "views/ViewController.h"
#include "SystemData.h"
#include <boost/filesystem.hpp>
#include "guis/GuiDetectDevice.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiMsgBoxScroll.h"
#include "AudioManager.h"
#include "platform.h"
#include "Log.h"
#include "Window.h"
#include "EmulationStation.h"
#include "RecalboxSystem.h"
#include "Settings.h"
#include "ScraperCmdLine.h"
#include "VolumeControl.h"
#include <sstream>
#include "LocaleES.h"
#include <boost/algorithm/string.hpp>
#include <RecalboxConf.h>
#include "resources/Font.h"
#include "NetworkThread.h"
#include "RecalboxSystem.h"
#include "FileSorts.h"


#ifdef WIN32
#include <Windows.h>
#include <direct.h> // getcwd, chdir
#define PATH_MAX 256
#endif

namespace fs = boost::filesystem;

bool scrape_cmdline = false;

void playSound(std::string name);

bool parseArgs(int argc, char* argv[], unsigned int* width, unsigned int* height)
{
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--resolution") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid resolution supplied.";
				return false;
			}

			*width = atoi(argv[i + 1]);
			*height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
		}else if(strcmp(argv[i], "--gamelist-only") == 0)
		{
			Settings::getInstance()->setBool("ParseGamelistOnly", true);
		}else if(strcmp(argv[i], "--ignore-gamelist") == 0)
		{
			Settings::getInstance()->setBool("IgnoreGamelist", true);
		}else if(strcmp(argv[i], "--draw-framerate") == 0)
		{
			Settings::getInstance()->setBool("DrawFramerate", true);
		}else if(strcmp(argv[i], "--no-exit") == 0)
		{
			Settings::getInstance()->setBool("ShowExit", false);
		}else if(strcmp(argv[i], "--debug") == 0)
		{
			Settings::getInstance()->setBool("Debug", true);
			Settings::getInstance()->setBool("HideConsole", false);
			Log::setReportingLevel(LogDebug);
		}else if(strcmp(argv[i], "--windowed") == 0)
		{
			Settings::getInstance()->setBool("Windowed", true);
		}else if(strcmp(argv[i], "--vsync") == 0)
		{
			bool vsync = (strcmp(argv[i + 1], "on") == 0 || strcmp(argv[i + 1], "1") == 0) ? true : false;
			Settings::getInstance()->setBool("VSync", vsync);
			i++; // skip vsync value
		}else if(strcmp(argv[i], "--scrape") == 0)
		{
			scrape_cmdline = true;
		}else if(strcmp(argv[i], "--max-vram") == 0)
		{
			int maxVRAM = atoi(argv[i + 1]);
			Settings::getInstance()->setInt("MaxVRAM", maxVRAM);
		}else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
#ifdef WIN32
			// This is a bit of a hack, but otherwise output will go to nowhere
			// when the application is compiled with the "WINDOWS" subsystem (which we usually are).
			// If you're an experienced Windows programmer and know how to do this
			// the right way, please submit a pull request!
			AttachConsole(ATTACH_PARENT_PROCESS);
			freopen("CONOUT$", "wb", stdout);
#endif
			std::cout << 
				"EmulationStation, a graphical front-end for ROM browsing.\n"
				"Written by Alec \"Aloshi\" Lofquist.\n"
				"Version " << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING << "\n\n"
				"Command line arguments:\n"
				"--resolution [width] [height]	try and force a particular resolution\n"
				"--gamelist-only			skip automatic game search, only read from gamelist.xml\n"
				"--ignore-gamelist		ignore the gamelist (useful for troubleshooting)\n"
				"--draw-framerate		display the framerate\n"
				"--no-exit			don't show the exit option in the menu\n"
				"--debug				more logging, show console on Windows\n"
				"--scrape			scrape using command line interface\n"
				"--windowed			not fullscreen, should be used with --resolution\n"
				"--vsync [1/on or 0/off]		turn vsync on or off (default is on)\n"
				"--max-vram [size]		Max VRAM to use in Mb before swapping. 0 for unlimited\n"
				"--help, -h			summon a sentient, angry tuba\n\n"
				"More information available in README.md.\n";
			return false; //exit after printing help
		}
	}

	return true;
}

bool verifyHomeFolderExists()
{
	//make sure the config directory exists
	std::string home = getHomePath();
	std::string configDir = home + "/.emulationstation";
	if(!fs::exists(configDir))
	{
		std::cout << "Creating config directory \"" << configDir << "\"\n";
		fs::create_directory(configDir);
		if(!fs::exists(configDir))
		{
			std::cerr << "Config directory could not be created!\n";
			return false;
		}
	}

	return true;
}

// Returns true if everything is OK, 
bool loadSystemConfigFile(const char** errorString)
{
	*errorString = NULL;

	if(!SystemData::loadConfig())
	{
		LOG(LogError) << "Error while parsing systems configuration file!";
		*errorString = "IT LOOKS LIKE YOUR SYSTEMS CONFIGURATION FILE HAS NOT BEEN SET UP OR IS INVALID. YOU'LL NEED TO DO THIS BY HAND, UNFORTUNATELY.";
		return false;
	}

	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "No systems found! Does at least one system have a game present? (check that extensions match!)\n(Also, make sure you've updated your es_systems.cfg for XML!)";
		*errorString = "WE CAN'T FIND ANY SYSTEMS!\n"
		  "CHECK THAT YOUR PATHS ARE CORRECT IN THE SYSTEMS CONFIGURATION FILE, AND "
		  "YOUR GAME DIRECTORY HAS AT LEAST ONE GAME WITH THE CORRECT EXTENSION.";
		return false;
	}

	return true;
}

//called on exit, assuming we get far enough to have the log initialized
void onExit()
{
	Log::close();
}

int setLocale(char * argv1)
{
 	char path_save[PATH_MAX];
  	char abs_exe_path[PATH_MAX];
  	char *p;

    if(!(p = strrchr(argv1, '/'))) {
    		getcwd(abs_exe_path, sizeof(abs_exe_path));
    }
  	else
  	{
    		*p = '\0';
    		getcwd(path_save, sizeof(path_save));
    		chdir(argv1);
    		getcwd(abs_exe_path, sizeof(abs_exe_path));
    		chdir(path_save);
  	}
	boost::locale::localization_backend_manager my = boost::locale::localization_backend_manager::global(); 
	// Get global backend

    	my.select("std");
	boost::locale::localization_backend_manager::global(my);
    	// set this backend globally

    	boost::locale::generator gen;

	std::string localeDir = abs_exe_path;
	localeDir += "/locale/lang";
	LOG(LogInfo) << "Setting local directory to " << localeDir;
    	// Specify location of dictionaries
    	gen.add_messages_path(localeDir);
    	gen.add_messages_path("/usr/share/locale");
    	gen.add_messages_domain("emulationstation2");

    	// Generate locales and imbue them to iostream
    	std::locale::global(gen(""));
    	std::cout.imbue(std::locale());
        LOG(LogInfo) << "Locals set...";
    return 0;
}

int main(int argc, char* argv[])
{
	unsigned int width = 0;
	unsigned int height = 0;

	//std::locale::global(boost::locale::generator().generate(""));
	//boost::filesystem::path::imbue(std::locale());

	if(!parseArgs(argc, argv, &width, &height))
		return 0;

	// only show the console on Windows if HideConsole is false
#ifdef WIN32
	// MSVC has a "SubSystem" option, with two primary options: "WINDOWS" and "CONSOLE".
	// In "WINDOWS" mode, no console is automatically created for us.  This is good, 
	// because we can choose to only create the console window if the user explicitly 
	// asks for it, preventing it from flashing open and then closing.
	// In "CONSOLE" mode, a console is always automatically created for us before we
	// enter main. In this case, we can only hide the console after the fact, which
	// will leave a brief flash.
	// TL;DR: You should compile ES under the "WINDOWS" subsystem.
	// I have no idea how this works with non-MSVC compilers.
	if(!Settings::getInstance()->getBool("HideConsole"))
	{
		// we want to show the console
		// if we're compiled in "CONSOLE" mode, this is already done.
		// if we're compiled in "WINDOWS" mode, no console is created for us automatically;
		// the user asked for one, so make one and then hook stdin/stdout/sterr up to it
		if(AllocConsole()) // should only pass in "WINDOWS" mode
		{
			freopen("CONIN$", "r", stdin);
			freopen("CONOUT$", "wb", stdout);
			freopen("CONOUT$", "wb", stderr);
		}
	}else{
		// we want to hide the console
		// if we're compiled with the "WINDOWS" subsystem, this is already done.
		// if we're compiled with the "CONSOLE" subsystem, a console is already created; 
		// it'll flash open, but we hide it nearly immediately
		if(GetConsoleWindow()) // should only pass in "CONSOLE" mode
			ShowWindow(GetConsoleWindow(), SW_HIDE);
	}
#endif

	//if ~/.emulationstation doesn't exist and cannot be created, bail
	if(!verifyHomeFolderExists())
		return 1;

	//start the logger
	Log::open();
	LOG(LogInfo) << "EmulationStation - v" << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING;

	//always close the log on exit
	atexit(&onExit);

	// Set locale
	setLocale(argv[0]);

	// other init
	FileSorts::init(); // require locale
	initMetadata(); // require locale
	
    Renderer::init(width, height);
	Window window;
	ViewController::init(&window);
	window.pushGui(ViewController::get());

	if(!scrape_cmdline)
    {
        if(!window.init(width, height, false))
		{
			LOG(LogError) << "Window failed to initialize!";
			return 1;
		}

		std::string glExts = (const char*)glGetString(GL_EXTENSIONS);
		LOG(LogInfo) << "Checking available OpenGL extensions...";
		LOG(LogInfo) << " ARB_texture_non_power_of_two: " << (glExts.find("ARB_texture_non_power_of_two") != std::string::npos ? "OK" : "MISSING");

		window.renderLoadingScreen();
	}

	// Initialize audio manager
	VolumeControl::getInstance()->init();
	AudioManager::getInstance()->init();

	playSound("loading");

	const char* errorMsg = NULL;
	if(!loadSystemConfigFile(&errorMsg))
	{
		// something went terribly wrong
		if(errorMsg == NULL)
		{
			LOG(LogError) << "Unknown error occured while parsing system config file.";
			if(!scrape_cmdline)
				Renderer::deinit();
			return 1;
		}

		// we can't handle es_systems.cfg file problems inside ES itself, so display the error message then quit
		window.pushGui(new GuiMsgBox(&window,
			errorMsg,
					     _("QUIT"), [] { 
				SDL_Event* quit = new SDL_Event();
				quit->type = SDL_QUIT;
				SDL_PushEvent(quit);
			}));
	}

	RecalboxConf* recalboxConf = RecalboxConf::getInstance();
	if(recalboxConf->get("kodi.enabled") == "1" && recalboxConf->get("kodi.atstartup") == "1"){
		RecalboxSystem::getInstance()->launchKodi(&window);
	}
	RecalboxSystem::getInstance()->getIpAdress();
	// UPDATED VERSION MESSAGE
	//if(RecalboxSystem::getInstance()->needToShowVersionMessage()){
	//	 window.pushGui(new GuiMsgBoxScroll(&window,
	//	RecalboxSystem::getInstance()->getVersionMessage(),
	//				      _("OK"), [] {
	//				 RecalboxSystem::getInstance()->updateLastVersionFile();
	//				},"",nullptr,"",nullptr, ALIGN_LEFT));
	//}

	// UPDATE CHECK THREAD
	if(recalboxConf->get("updates.enabled") == "1"){
		NetworkThread * nthread = new NetworkThread(&window);
	}

	//run the command line scraper then quit
	if(scrape_cmdline)
	{
		return run_scraper_cmdline();
	}

	//dont generate joystick events while we're loading (hopefully fixes "automatically started emulator" bug)
	SDL_JoystickEventState(SDL_DISABLE);



	// preload what we can right away instead of waiting for the user to select it
	// this makes for no delays when accessing content, but a longer startup time
	//ViewController::get()->preload();

	//choose which GUI to open depending on if an input configuration already exists
	if(errorMsg == NULL)
	{
		if(fs::exists(InputManager::getConfigPath()) && InputManager::getInstance()->getNumConfiguredDevices() > 0)
		{
			ViewController::get()->goToStart();
		}else{
			window.pushGui(new GuiDetectDevice(&window, true, [] { ViewController::get()->goToStart(); }));
		}
	}

	// Create a flag in  temporary directory to signal READY state
	fs::path ready_path = fs::temp_directory_path();
	ready_path /= "emulationstation.ready";
	FILE* ready_file = fopen(ready_path.generic_string().c_str(), "w");
	if(ready_file) fclose(ready_file);

	//generate joystick events since we're done loading
	SDL_JoystickEventState(SDL_ENABLE);

	int lastTime = SDL_GetTicks();
	bool running = true;
	bool doReboot = false;
	bool doShutdown = false;
	
	while(running)
	{
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_JOYHATMOTION:
				case SDL_JOYBUTTONDOWN:
				case SDL_JOYBUTTONUP:
				case SDL_KEYDOWN:
				case SDL_KEYUP:
				case SDL_JOYAXISMOTION:
				case SDL_TEXTINPUT:
				case SDL_TEXTEDITING:
				case SDL_JOYDEVICEADDED:
				case SDL_JOYDEVICEREMOVED:
					InputManager::getInstance()->parseEvent(event, &window);
					break;
				case SDL_QUIT:
					running = false;
					break;
				case RecalboxSystem::SDL_FAST_QUIT | RecalboxSystem::SDL_RB_REBOOT:
					running = false;
					doReboot = true;
					Settings::getInstance()->setBool("IgnoreGamelist", true);
					break;
				case RecalboxSystem::SDL_FAST_QUIT | RecalboxSystem::SDL_RB_SHUTDOWN:
					running = false;
					doShutdown = true;
					Settings::getInstance()->setBool("IgnoreGamelist", true);
					break;
				case SDL_QUIT | RecalboxSystem::SDL_RB_REBOOT:
					running = false;
					doReboot = true;
					break;
				case SDL_QUIT | RecalboxSystem::SDL_RB_SHUTDOWN:
					running = false;
					doShutdown = true;
					break;
			}
		}

		if(window.isSleeping())
		{
			lastTime = SDL_GetTicks();
			SDL_Delay(1); // this doesn't need to be accurate, we're just giving up our CPU time until something wakes us up
			continue;
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;
		lastTime = curTime;

		// cap deltaTime at 1000
		if(deltaTime > 1000 || deltaTime < 0)
			deltaTime = 1000;

		window.update(deltaTime);
		window.render();
		Renderer::swapBuffers();

		Log::flush();
	}

	// Clean ready flag
	if(fs::exists(ready_path)) fs::remove(ready_path);

	while(window.peekGui() != ViewController::get())
		delete window.peekGui();

	window.renderShutdownScreen();
	SystemData::deleteSystems();
	window.deinit();
	LOG(LogInfo) << "EmulationStation cleanly shutting down.";
	if (doReboot) {
		LOG(LogInfo) << "Rebooting system";
		system("touch /tmp/reboot.please");
		system("shutdown -r now");
	} else if (doShutdown) {
		LOG(LogInfo) << "Shutting system down";
		system("touch /tmp/shutdown.please");
		system("shutdown -h now");
	}

	return 0;
}

void playSound(std::string name) {
	std::string selectedTheme = Settings::getInstance()->getString("ThemeSet");
	std::string loadingMusic = getHomePath()+"/.emulationstation/themes/"+selectedTheme+"/fx/"+name+".ogg";
	if(boost::filesystem::exists(loadingMusic)){
		Music::get(loadingMusic)->play(false, NULL);
	}
}

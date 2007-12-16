/*=============================================================================
XMOTO

This file is part of XMOTO.

XMOTO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

XMOTO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XMOTO; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

/* 
 *  Game application. (init-related stuff)
 */
 
/* rneckelmann 2006-09-30: moved a lot of stuff from Game.cpp into here to 
                           make it a tad smaller */ 
#include "GameText.h"
#include "Game.h"
#include "VFileIO.h"
#include "Sound.h"
#include "PhysSettings.h"
#include "Input.h"
#include "db/xmDatabase.h";
#include "helpers/Log.h"

#include <curl/curl.h>
#include "XMSession.h"
#include "XMArgs.h"
#include "drawlib/DrawLib.h"
#include "Packager.h"
#include "helpers/SwapEndian.h"
#include "SysMessage.h"
#include "gui/specific/GUIXMoto.h"
#include "Credits.h"

#include "states/StateManager.h"
#include "states/StateEditProfile.h"
#include "states/StateReplaying.h"
#include "states/StatePreplaying.h"
#include "states/StateMainMenu.h"
#include "states/StateMessageBox.h"

#if defined(WIN32)
int SDL_main(int nNumArgs,char **ppcArgs) {
#else
int main(int nNumArgs,char **ppcArgs) {
#endif
  /* Start application */
  try {     
    /* Setup basic info */
    GameApp vapp;

    vapp.run(nNumArgs,ppcArgs);
  }
  catch (Exception &e) {
    if(Logger::isInitialized()) {
      Logger::Log((std::string("Exception: ") + e.getMsg()).c_str());
    }    

    printf("fatal exception : %s\n",e.getMsg().c_str());        
    SDL_Quit(); /* make sure SDL shuts down gracefully */
    
#if defined(WIN32)
    char cBuf[1024];
    sprintf(cBuf,"Fatal exception occured: %s\n"
	    "Consult the file xmoto.log for more information about what\n"
	    "might has occured.\n",e.getMsg().c_str());                    
    MessageBox(NULL,cBuf,"X-Moto Error",MB_OK|MB_ICONERROR);
#endif
  }
  return 0;
}

void GameApp::run(int nNumArgs,char **ppcArgs) {
  run_load(nNumArgs,ppcArgs);
  run_loop();
  run_unload();
}

void GameApp::run_load(int nNumArgs,char **ppcArgs) {
  XMArguments v_xmArgs;
  GameState* pState;

  /* check args */
  try {
    v_xmArgs.parse(nNumArgs, ppcArgs);
  } catch (Exception &e) {
    printf("syntax error : %s\n", e.getMsg().c_str());
    v_xmArgs.help(nNumArgs >= 1 ? ppcArgs[0] : "xmoto");
    quit();
    return; /* abort */
  }

  /* help */
  if(v_xmArgs.isOptHelp()) {
    v_xmArgs.help(nNumArgs >= 1 ? ppcArgs[0] : "xmoto");
    quit();
    return;
  }

  /* init sub-systems */
  SwapEndian::Swap_Init();
  srand(time(NULL));

  /* package / unpackage */
  if(v_xmArgs.isOptPack()) {
    Packager::go(v_xmArgs.getOpt_pack_bin() == "" ? "xmoto.bin" : v_xmArgs.getOpt_pack_bin(),
		 v_xmArgs.getOpt_pack_dir() == "" ? "."         : v_xmArgs.getOpt_pack_dir());
    quit();
    return;
  }
  if(v_xmArgs.isOptUnPack()) {
    Packager::goUnpack(v_xmArgs.getOpt_unpack_bin() == "" ? "xmoto.bin" : v_xmArgs.getOpt_unpack_bin(),
		       v_xmArgs.getOpt_unpack_dir() == "" ? "."         : v_xmArgs.getOpt_unpack_dir(),
		       v_xmArgs.getOpt_unpack_noList() == false);
    quit();
    return;
  }
  /* ***** */

  if(v_xmArgs.isOptConfigPath()) {
    FS::init("xmoto", "xmoto.bin", "xmoto.log", v_xmArgs.getOpt_configPath_path());
  } else {
    FS::init("xmoto", "xmoto.bin", "xmoto.log");
  }
  Logger::init(FS::getUserDir() + "/xmoto.log");

  /* load config file, the session */
  XMSession::createDefaultConfig(&m_Config);
  m_Config.loadFile();
  m_xmsession->load(&m_Config); /* overload default session by userConfig */
  m_xmsession->load(&v_xmArgs); /* overload default session by xmargs     */
  Logger::setVerbose(m_xmsession->isVerbose()); /* apply verbose mode */

#ifdef USE_GETTEXT
    std::string v_locale = Locales::init(m_xmsession->language());
#endif

  Logger::Log("compiled at "__DATE__" "__TIME__);
  if(SwapEndian::bigendien) {
    Logger::Log("Systeme is bigendien");
  } else {
    Logger::Log("Systeme is littleendien");
  }
  Logger::Log("User directory: %s", FS::getUserDir().c_str());
  Logger::Log("Data directory: %s", FS::getDataDir().c_str());

#ifdef USE_GETTEXT
  Logger::Log("Locales set to '%s' (directory '%s')", v_locale.c_str(), LOCALESDIR);
#endif

  if(v_xmArgs.isOptListLevels() || v_xmArgs.isOptListReplays() || v_xmArgs.isOptReplayInfos()) {
    m_xmsession->setUseGraphics(false);
  }

  _InitWin(m_xmsession->useGraphics());

  if(m_xmsession->useGraphics()) {
    /* init drawLib */
    drawLib = DrawLib::DrawLibFromName(m_xmsession->drawlib());
    
    if(drawLib == NULL) {
      throw Exception("Drawlib not initialized");
    }

    m_Renderer = new GameRenderer(drawLib);
    m_Renderer->setTheme(&m_theme);
    m_MotoGame.setRenderer(m_Renderer);
    m_sysMsg = new SysMessage(drawLib);
    
    drawLib->setNoGraphics(m_xmsession->useGraphics() == false);
    drawLib->setDontUseGLExtensions(m_xmsession->glExts() == false);

    /* Init! */
    drawLib->init(m_xmsession->resolutionWidth(), m_xmsession->resolutionHeight(), m_xmsession->bpp(), m_xmsession->windowed(), &m_theme);
    /* drawlib can change the final resolution if it fails, then, reinit session one's */
    m_xmsession->setResolutionWidth(drawLib->getDispWidth());
    m_xmsession->setResolutionHeight(drawLib->getDispHeight());
    m_xmsession->setBpp(drawLib->getDispBPP());
    m_xmsession->setWindowed(drawLib->getWindowed());
    Logger::Log("Resolution: %ix%i (%i bpp)", m_xmsession->resolutionWidth(), m_xmsession->resolutionHeight(), m_xmsession->bpp());
    /* */
    
    if(!drawLib->isNoGraphics()) {        
      drawLib->setDrawDims(m_xmsession->resolutionWidth(), m_xmsession->resolutionHeight(),
			   m_xmsession->resolutionWidth(), m_xmsession->resolutionHeight());
    }
  }

  /* Init sound system */
  Sound::init(m_xmsession);

  /* Init renderer */
  if(m_xmsession->useGraphics()) {
    m_stateManager = new StateManager(this);
    switchUglyMode(m_xmsession->ugly());
    switchTestThemeMode(m_xmsession->testTheme());
    m_Renderer->setParent( (GameApp *)this );
    m_Renderer->setGameObject( &m_MotoGame );        
  }    

  /* Tell collision system whether we want debug-info or not */
  m_MotoGame.getCollisionHandler()->setDebug(m_xmsession->debug());

  if(m_xmsession->useGraphics()) {
    if(m_xmsession->gDebug()) m_Renderer->loadDebugInfo(m_xmsession->gDebugFile());
  }

  /* database */
  m_db = new xmDatabase(DATABASE_FILE,
			m_xmsession->profile() == "" ? std::string("") : m_xmsession->profile(),
			FS::getDataDir(), FS::getUserDir(), FS::binCheckSum(),
			m_xmsession->useGraphics() ? this : NULL);
  if(m_xmsession->sqlTrace()) {
    m_db->setTrace(m_xmsession->sqlTrace());
  }

  /* load theme */
  m_themeChoicer = new ThemeChoicer();
  if(m_db->themes_isIndexUptodate() == false) {
    m_themeChoicer->initThemesFromDir(m_db);
  }
  try {
    reloadTheme();
  } catch(Exception &e) {
    /* if the theme cannot be loaded, try to reload from files */
    /* perhaps that the xm.db comes from an other computer */
    Logger::Log("** warning ** : Theme cannot be reload, try to update themes into the database");
    m_themeChoicer->initThemesFromDir(m_db);
    reloadTheme();
  }
  
  /* load levels */
  if(m_db->levels_isIndexUptodate() == false) {
      m_levelsManager.reloadLevelsFromLvl(m_db, m_xmsession->useGraphics() ? this : NULL);
  }
  m_levelsManager.reloadExternalLevels(m_db, m_xmsession->useGraphics() ? this : NULL);
  
  /* Update replays */
  if(m_db->replays_isIndexUptodate() == false) {
    initReplaysFromDir();
  }
  
  /* List replays? */  
  if(v_xmArgs.isOptListReplays()) {
    char **v_result;
    unsigned int nrow;
    
    printf("\nReplay                    Level                     Player\n");
    printf("-----------------------------------------------------------------------\n");
    
    v_result = m_db->readDB("SELECT a.name, a.id_profile, b.name "
			    "FROM replays AS a INNER JOIN levels AS b "
			    "ON a.id_level = b.id_level;", nrow);
    if(nrow == 0) {
      printf("(none)\n");
    } else {
	for(unsigned int i=0; i<nrow; i++) {
	  //m_db->getResult(v_result, 4, i, 0)
	  printf("%-25s %-25s %-25s\n",
		 m_db->getResult(v_result, 3, i, 0),
		 m_db->getResult(v_result, 3, i, 2),
		 m_db->getResult(v_result, 3, i, 1)
		 );
	}
    }
    m_db->read_DB_free(v_result);
    quit();
    return;
  }
  
  if(v_xmArgs.isOptReplayInfos()) {
    Replay v_replay;
    std::string v_levelId;
      std::string v_player;
    
    v_levelId = v_replay.openReplay(v_xmArgs.getOpt_replayInfos_file(), v_player, true);
    if(v_levelId == "") {
      throw Exception("Invalid replay");
    }
    
    quit();
    return;	
  }
  
  if(v_xmArgs.isOptLevelID()) {
    m_PlaySpecificLevelId = v_xmArgs.getOpt_levelID_id();
  }
  if(v_xmArgs.isOptLevelFile()) {
    m_PlaySpecificLevelFile = v_xmArgs.getOpt_levelFile_file();
  }
  if(v_xmArgs.isOptReplay()) {
    m_PlaySpecificReplay = v_xmArgs.getOpt_replay_file();
  }
  
  if(m_xmsession->useGraphics()) {  
    _UpdateLoadingScreen(0, GAMETEXT_LOADINGSOUNDS);
    
    /* Load sounds */
    try {
      for(unsigned int i=0; i<m_theme.getSoundsList().size(); i++) {
	Sound::loadSample(m_theme.getSoundsList()[i]->FilePath());
      }
    } catch(Exception &e) {
      Logger::Log("*** Warning *** : %s\n", e.getMsg().c_str());
      /* hum, not cool */
    }
    
    Logger::Log(" %d sound%s loaded",Sound::getNumSamples(),Sound::getNumSamples()==1?"":"s");
    
    /* Find all files in the textures dir and load them */     
    UITexture::setApp(this);
    UIWindow::setDrawLib(getDrawLib());
    
    _UpdateLoadingScreen((1.0f/9.0f) * 2,GAMETEXT_LOADINGMENUGRAPHICS);
  }
  
  /* Should we clean the level cache? (can also be done when disabled) */
  if(v_xmArgs.isOptCleanCache()) {
    LevelsManager::cleanCache();
  }
  
  /* -listlevels? */
  if(v_xmArgs.isOptListLevels()) {
    m_levelsManager.printLevelsList(m_db);
    quit();
    return;
  }
  
  /* requires graphics now */
  if(m_xmsession->useGraphics() == false) {
    quit();
    return;
  }
  
  /* Initialize renderer */
  _UpdateLoadingScreen((1.0f/9.0f) * 6,GAMETEXT_INITRENDERER);
  m_Renderer->init();
  
  /* build handler */
  m_InputHandler.init(&m_Config);
  Replay::enableCompression(m_xmsession->compressReplays());
  
  /* load packs */
  LevelsManager::checkPrerequires();
  m_levelsManager.makePacks(m_db, m_xmsession->profile(), m_xmsession->idRoom(), m_xmsession->debug());
  
  /* What to do? */
  if(m_PlaySpecificLevelFile != "") {
    try {
      m_levelsManager.addExternalLevel(m_db, m_PlaySpecificLevelFile);
      m_PlaySpecificLevelId = m_levelsManager.LevelByFileName(m_db, m_PlaySpecificLevelFile);
    } catch(Exception &e) {
      m_PlaySpecificLevelId = m_PlaySpecificLevelFile;
    }
  }
  if((m_PlaySpecificLevelId != "")) {
    /* ======= PLAY SPECIFIC LEVEL ======= */
    m_stateManager->pushState(new StatePreplaying(this, m_PlaySpecificLevelId, false));
    Logger::Log("Playing as '%s'...", m_xmsession->profile().c_str());
  }
  else if(m_PlaySpecificReplay != "") {
    /* ======= PLAY SPECIFIC REPLAY ======= */
    m_stateManager->pushState(new StateReplaying(this, m_PlaySpecificReplay));
    }
  else {
    /* display what must be displayed */
    StateMainMenu* pMainMenu = new StateMainMenu(this);
    m_stateManager->pushState(pMainMenu);
    
    /* Do we have a player profile? */
    if(m_xmsession->profile() == "") {
      m_stateManager->pushState(new StateEditProfile(this, pMainMenu));
    } 
    
    /* Should we show a notification box? (with important one-time info) */
    if(m_xmsession->notifyAtInit()) {
      m_stateManager->pushState(new StateMessageBox(NULL, this, GAMETEXT_NOTIFYATINIT, UI_MSGBOX_OK));
      m_xmsession->setNotifyAtInit(false);
    }
  }
  
  if (m_xmsession->ugly()){
    drawLib->clearGraphics();
  }
  drawFrame();
  drawLib->flushGraphics();
  
  /* Update stats */
  if(m_xmsession->profile() != "") {
    m_db->stats_xmotoStarted(m_xmsession->profile());
  }
  
  Logger::Log("UserInit ended at %.3f", GameApp::getXMTime());
}

void GameApp::run_loop() {
  while(!m_bQuit) {
    /* Handle SDL events */            
    SDL_PumpEvents();
    
    SDL_Event Event;
    while(SDL_PollEvent(&Event)) {
      int ch=0;
      static int nLastMouseClickX = -100,nLastMouseClickY = -100;
      static int nLastMouseClickButton = -100;
      static float fLastMouseClickTime = 0.0f;
      int nX,nY;

      /* What event? */
      switch(Event.type) {
      case SDL_KEYDOWN: 
	if((Event.key.keysym.unicode&0xff80)==0) {
	  ch = Event.key.keysym.unicode & 0x7F;
	}
	keyDown(Event.key.keysym.sym, Event.key.keysym.mod, ch);            
	break;
      case SDL_KEYUP: 
	keyUp(Event.key.keysym.sym, Event.key.keysym.mod);            
	break;
      case SDL_QUIT:  
	/* Force quit */
	quit();
	break;
      case SDL_MOUSEBUTTONDOWN:
	/* Pass ordinary click */
	mouseDown(Event.button.button);
              
	/* Is this a double click? */
	getMousePos(&nX,&nY);
	if(nX == nLastMouseClickX &&
	   nY == nLastMouseClickY &&
	   nLastMouseClickButton == Event.button.button &&
	   (getXMTime() - fLastMouseClickTime) < 0.250f) {                
	    
	  /* Pass double click */
	  mouseDoubleClick(Event.button.button);                
	}
	fLastMouseClickTime = getXMTime();
	nLastMouseClickX = nX;
	nLastMouseClickY = nY;
	nLastMouseClickButton = Event.button.button;
	  
	break;
      case SDL_MOUSEBUTTONUP:
	mouseUp(Event.button.button);
	break;
      }

    }

    /* Update user app */
    drawFrame();

    _Wait();
  }
}

void GameApp::run_unload() {
  if(m_pWebHighscores != NULL) {
    delete m_pWebHighscores;
  }        

  if(m_pWebLevels != NULL) {
    delete m_pWebLevels;
  }    

  if(m_Renderer != NULL) {
    m_Renderer->unprepareForNewLevel(); /* just to be sure, shutdown can happen quite hard */
    m_Renderer->shutdown();
    m_InputHandler.uninit();
  }

  if(m_themeChoicer != NULL) {
    delete m_themeChoicer;
  }
    
  if(m_pJustPlayReplay != NULL) {
    delete m_pJustPlayReplay;
  }    

  if(m_stateManager != NULL) {
    delete m_stateManager;
  }
  
  if(Sound::isInitialized()) {
    Sound::uninit();
  }

  if(m_Renderer != NULL) {
    delete m_Renderer;
  }
  
  if(m_sysMsg != NULL) {
    delete m_sysMsg;
  }

  if(drawLib != NULL) { /* save config only if drawLib was initialized */
    m_xmsession->save(&m_Config);
    m_InputHandler.saveConfig(&m_Config);
    m_Config.saveFile();
  }

  if(drawLib != NULL) {
    drawLib->unInit();
  }
    
  if(Logger::isInitialized()) {
    Logger::uninit();
  }
    
  /* Shutdown SDL */
  SDL_Quit();
}


void GameApp::_Wait()
{
  if(m_lastFrameTimeStamp < 0){
    m_lastFrameTimeStamp = getXMTimeInt();
  }

  /* Does app want us to delay a bit after the frame? */
  int currentTimeStamp        = getXMTimeInt();
  int currentFrameMinDuration = 1000/m_stateManager->getMaxFps();
  int lastFrameDuration       = currentTimeStamp - m_lastFrameTimeStamp;
  // late from the lasts frame is not forget
  int delta = currentFrameMinDuration - (lastFrameDuration + m_frameLate);

  if(delta > 0){
    // we're in advance
    // -> sleep
    int beforeSleep = getXMTimeInt();
    SDL_Delay(delta);
    int afterSleep  = getXMTimeInt();
    int sleepTime   = afterSleep - beforeSleep;

    // now that we have sleep, see if we don't have too much sleep
    if(sleepTime > delta){
      int tooMuchSleep = sleepTime - delta;
      m_frameLate      = tooMuchSleep;
    } else{
      m_frameLate = 0;
    }
  }
  else{
    // we're late
    // -> update late time
    m_frameLate = (-delta);
  }

  // the sleeping time is not included in the next frame time
  m_lastFrameTimeStamp = getXMTimeInt();
}


  /*===========================================================================
  Update loading screen
  ===========================================================================*/
  void GameApp::_UpdateLoadingScreen(float fDone, const std::string &NextTask) {
    FontManager* v_fm;
    FontGlyph* v_fg;
    int v_border = 3;
    int v_fh;

    getDrawLib()->clearGraphics();
    getDrawLib()->resetGraphics();

    v_fm = getDrawLib()->getFontBig();
    v_fg = v_fm->getGlyph(GAMETEXT_LOADING);
    v_fh = v_fg->realHeight();
    v_fm->printString(v_fg,
		      getDrawLib()->getDispWidth()/2 - 256,
		      getDrawLib()->getDispHeight()/2 - 30,
		      MAKE_COLOR(255,255,255, 255));

    getDrawLib()->drawBox(Vector2f(getDrawLib()->getDispWidth()/2 - 256,
				   getDrawLib()->getDispHeight()/2 - 30),              
			  Vector2f(getDrawLib()->getDispWidth()/2 - 256 + (512.0f*fDone),
				   getDrawLib()->getDispHeight()/2 - 30 + v_border),
			  0,MAKE_COLOR(255,255,255,255));

    getDrawLib()->drawBox(Vector2f(getDrawLib()->getDispWidth()/2 - 256,
				   getDrawLib()->getDispHeight()/2 -30 + v_fh),              
			  Vector2f(getDrawLib()->getDispWidth()/2 - 256 + (512.0f*fDone),
				   getDrawLib()->getDispHeight()/2 - 30 + v_fh + v_border),
			  0,MAKE_COLOR(255,255,255,255));
    
    v_fm = getDrawLib()->getFontSmall();
    v_fg = v_fm->getGlyph(NextTask);
    v_fm->printString(v_fg,
		      getDrawLib()->getDispWidth()/2 - 256,
		      getDrawLib()->getDispHeight()/2 -30 + v_fh + 2,
		      MAKE_COLOR(255,255,255,255));      
    getDrawLib()->flushGraphics();
  }

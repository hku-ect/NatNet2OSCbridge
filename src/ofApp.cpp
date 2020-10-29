#include "ofApp.h"
#include "fontawesome5.h"
#include "version.h"
#include "themes.h"
#include "ect_helpers.h"

static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

//additions for velocities / angular velocity
RigidBodyHistory::RigidBodyHistory( int id, ofVec3f p, ofQuaternion r )
{
    previousPosition = p;
    previousOrientation = r;
    rigidBodyId = id;
    
    currentDataPoint = 0;
    firstRun = TRUE;
    framesInactive = 0;
}
//end

/*
 * VRTrackers:
 * - name
 * - rotation (quarternion)
 * - position
 * - velocity
 * - angular velocity
 */

//--------------------------------------------------------------
void ofApp::setup()
{
    ofSetLogLevel(OF_LOG_VERBOSE);
    ofLog::setChannel(uiLogWidget.channel);
    ofSetVerticalSync(true);
    ofBackground(67,67,67);

    //init python
    init_python();

    //fill the interface list
    auto niclist = listNetworkInterfaces( ANY, NetworkInterface::IPv4_ONLY );
    auto itr =  niclist.begin();
    while ( itr != niclist.end() )
    {
        iface_list.push_back( itr->name() );
        *itr++;
    }
    // and set the current iface to last found (first is probably lo)
    current_iface_idx = iface_list.size() - 1;
    setupConnectionInterface();

    string initialXml = "setup.xml";
    save_fileName = new char[32];
    strcpy(save_fileName, initialXml.c_str());
    
    setupData("");
    visible = true;
    numRigidBody = 0;
    numSkeleton = 0;
    running = true;
    
    rigidBodySize = -1;
    skeletonSize = -1;
    connected = false;
    triedToConnect = false;
    openModal = false;
    invFPS = 1.0f / FPS;

    // Setup OSC receive
    setupOSCReceiver();

    // SETUP GUI
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                  // no imgui.ini
    fontDefault = io.Fonts->AddFontDefault();
    ImFontConfig config;
    config.MergeMode = true;
    //config.GlyphMinAdvanceX = 13.0f; // Use if you want to make the icon monospaced
    config.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF(ofToDataPath( FONT_ICON_FILE_NAME_FAS ).data(), 12.0f, &config, icon_ranges);

    string t = ofFilePath::getAbsolutePath("verdana.ttf");
    fontSubTitle = io.Fonts->AddFontFromFileTTF(t.c_str(), 16.0f);
    fontTitle = io.Fonts->AddFontFromFileTTF(t.c_str(), 18.0f);
    gui.setup(new GuiGreenTheme(), false);              // default theme, no autoDraw!
    
    guiVisible = true;

    // VRTracker stuff -----------------------------------------------
    // Setup OpenVR and connect to the SteamVR server.
#if defined(_WIN32) || defined(_WIN64)
    openvr.connect();
    // Add a listener to receive new data
    ofAddListener(openvr.newDataReceived, this, &ofApp::newDeviceData);
#endif
    numConnectedVRTrackers = 0;
    
}

void ofApp::setupOSCReceiver()
{
    if ( oscRecv.isListening() )
    {
        oscRecv.stop();
    }
    oscRecv.setup(oscListenPort);
}

void ofApp::setupConnectionInterface(){
    
    // These variables position the connection interface
    InterfaceX = ofGetWidth()-350;
    InterfaceY = 50;
    
}

void ofApp::setupData( string filename="" )
{
    ofLogVerbose("SetupData:: connection");
    userDataDir = getAppConfigDir();
    string xmlpath = ofFilePath::join(userDataDir, save_fileName); //setup.xml );
    string persistentPath = ofFilePath::join(userDataDir, "persistent_state.xml");
    if ( filename == "" || !ofFile::doesFileExist( xmlpath ) )
    {
        if (!ofFile::doesFileExist(persistentPath)) {
            xmlpath = "setup.xml";
            ofLogVerbose() << "Loading setup from packaged " << xmlpath << " probably first run or never saved on this system?";
        }
        else {
            ofxXmlSettings state(persistentPath);
            state.pushTag("name");
            xmlpath = state.getValue("filename", "setup.xml");
            strcpy(save_fileName, xmlpath.c_str());
            xmlpath = ofFilePath::join(userDataDir, xmlpath);
            state.popTag();
        }
    }
    else if ( filename != "" ) {    //"" = load from initial setup
        //store name of last loaded file
        ofxXmlSettings persistent_state;
        persistent_state.addTag("name");
        persistent_state.pushTag("name", 0);
        persistent_state.addValue("filename", save_fileName);
        persistent_state.popTag();
        persistent_state.save(persistentPath);
    }

    ofxXmlSettings data( xmlpath );
    data.pushTag("setup",0);
    FPS = data.getValue("fps", 30);
    ofLogWarning("setupData :: fps: " + ofToString(FPS));
    
    string xml_interface = data.getValue("interface", "");
    std::vector<string>::iterator it = std::find( iface_list.begin(), iface_list.end(), xml_interface);
    if ( it != iface_list.end() )
    {
        // interface found on this machine
        current_iface_idx = std::distance(iface_list.begin(), it);
        ofLogWarning("setupData :: interface: " + xml_interface);
    }
    else
    {
        // interface not found, do nothing
        ofLogWarning("setupData :: interface: " + xml_interface + " not found, using default interface");
    }

    strncpy(natnetip_char, data.getValue("ip", "10.200.200.13").c_str(), 16);
    natnetip_char[sizeof(natnetip_char)-1] = 0;
    ofLogWarning("setupData :: natnetip: " + ofToString(natnetip_char));
    
    ofSetFrameRate(FPS);
    data.popTag();


    while( clients.size() > 0 ) {
        int i = 0;
        deleteClient(i);
    }

    int numClients = data.getNumTags("client");

    ofLogVerbose("SetupData:: clients (" + ofToString(numClients) + ")");
    for (int i = 0; i < numClients; i++)
    {
        data.pushTag("client",i);
        string ip = data.getValue("ip", "127.0.0.1");
        int port = data.getValue("port", 1234);
        string name = data.getValue("name", "unknown");
        bool r = data.getValue("rigid", 0);
        bool m = data.getValue("marker", 0);
        bool s = data.getValue("skeleton", 0);
        bool live = data.getValue("live", 0);
        bool hier = data.getValue("hierarchy", 0);
        bool midi = data.getValue("midi", 0);
        bool osc = data.getValue("osc", 0);
        int modeFlags = (int)data.getValue("mode", 0);
        bool vrt = data.getValue("vrtrackers", 0);
        addClient(i,ip,port,name,r,m,s,live,vrt, hier, modeFlags, midi, osc);
        data.popTag();
    }

    data.pushTag("midi");
    midiIn.currentDevice = data.getValue("currentDevice", 0);
    midiIn.currentDeviceName = data.getValue("currentDeviceName", "");
    midiIn.ignoreSysex = data.getValue("ignoreSysex", 0);
    midiIn.ignoreTiming = data.getValue("ignoreTiming", 0);
    midiIn.ignoreSense = data.getValue("ignoreSense", 0);
    midiIn.verbose = data.getValue("verbose", 0);
    midiIn.sendRaw = data.getValue("sendRaw", 0);
    data.popTag();

    data.pushTag("OSCReceive");
    oscListenPort = data.getValue("oscListenPort", 2525);
    data.popTag();
    ofLogVerbose("SetupData :: DONE");
}


bool ofApp::connectNatnet(string interfaceName, string interfaceIP)
{
    if(!triedToConnect) triedToConnect = true;
    
    natnet.setup(interfaceName, interfaceIP);  // interface name, server ip
    //natnet.setDuplicatedPointRemovalDistance(20);
    
    return false;
}


//--------------------------------------------------------------
void ofApp::update()
{
    if(running && natnet.isConnected() )
    {
        natnet.update();
        sendOSC();
        UserFeedback = "";
    }
    // Show user feedback
    else if(running == true && triedToConnect == true){
        
        UserFeedback  = "\n NatNet setup failed please check: \n";
        UserFeedback += " 1. Does Motive broadcast data over the right IP address? \n";
        UserFeedback += " 2. Did you enter the right remote IP address? \n";
        UserFeedback += " 3. Did you enter the right local interface?\n ";
        setFeedback(UserFeedback);
        triedToConnect = false;
    }
    if ( midiIn.midiIn.isOpen() )
    {
        sendMidi();
    }

    while (oscRecv.hasWaitingMessages() )
    {
        // get the next message
        ofxOscMessage m;
        oscRecv.getNextMessage(m);
        for( int i = 0; i < clients.size(); ++i )
        {
            clients[i]->sendData(m);
        }
    }

    if(natnet.isConnected()) connected = true;
    else connected = false;
    
    doGui();
}

//--------------------------------------------------------------
void ofApp::draw()
{
    if ( this->guiVisible ) { gui.draw(); }

    // Draw debug info to screen
    //ofDrawBitmapStringHighlight(out, 60, 300);
}

void ofApp::addClient(int i,string ip,int p,string n,bool r,bool m,bool s, bool live,bool vrt, bool hierarchy, int modeFlags, bool midi, bool osc)
{
    // Check if we do not add a client with the same properties twice
    bool uniqueClient = true;
    for (int i = 0; i < clients.size(); i++)
    {
        if(clients[i]->getIP() == ip && clients[i]->getPort() == p){
            uniqueClient = false;
            // Set feedback
            setFeedback("A client with the same settings already exists.\nPlease change the client's settings!\n");
            break;
        }
        
    }
    
    if(uniqueClient){
        client *c = new client(i,ip,p,n,r,m,s,live,vrt, hierarchy, modeFlags, midi, osc);
        ofAddListener(c->deleteClient, this, &ofApp::deleteClient);
        clients.push_back(c);
        if(UserFeedback != "") UserFeedback = "";
    }
}


// Set Feedback text and open pop-up
void ofApp::setFeedback(string feedbackText){
    UserFeedback = feedbackText;
    openModal = true;
}



void ofApp::sendOSC()
{
    //set to false if number of skels/rb's needs updating
    bool rigidbodiesReady = true;
    bool skeletonsReady = true;
    bool sentRequest = false;
    
    // get & check rigidbodies size
    vector<ofxNatNet::RigidBodyDescription> rbd = natnet.getRigidBodyDescriptions();
    int size = rbd.size();
    // set rigidBodySize to numbe rof rigidbodies we get from natnet.
    rigidBodySize = natnet.getNumRigidBody();
    // if there is a difference do natnet.sendRequestDescription(); to get up to date rigidbodie descriptions and thus names
    if (size != rigidBodySize)
    {
        natnet.sendRequestDescription();
        rigidBodySize = size;
        rigidbodiesReady = false;
        sentRequest = true;
    }
    
    //get & check skeletons size
    vector<ofxNatNet::SkeletonDescription> sd = natnet.getSkeletonDescriptions();
    size = sd.size();
    skeletonSize = natnet.getNumSkeleton();
    if (size != skeletonSize)
    {
        if ( !sentRequest ) natnet.sendRequestDescription();
        skeletonSize = size;
        skeletonsReady = false;
    }
    
    for( int i = 0; i < clients.size(); ++i )
    {
        ofxOscBundle bundle;
        
        //markers
        if ( clients[i]->getMarker() )
            getMarkers( clients[i], &bundle );
        
        //rigidbodies
        if ( rigidbodiesReady && clients[i]->getRigid() )
            getRigidbodies( clients[i], &bundle, rbd );
        
        //skeletons
        if ( skeletonsReady && clients[i]->getSkeleton() )
            getSkeletons( clients[i], &bundle, sd );

        //TODO: add vivetrackers data
        
        //check if not empty & send
        if ( bundle.getMessageCount() > 0 )
        {
            clients[i]->sendBundle(bundle);
        }
    }
}

void ofApp::sendMidi()
{
    for( int i = 0; i < clients.size(); ++i )
    {
        ofxOscBundle bundle;
        //  midi
        if ( clients[i]->getMidiFlag() && midiIn.midiMessages.size() > 0 )
        {
            for(unsigned int i = 0; i < midiIn.midiMessages.size(); ++i)
            {
                ofxMidiMessage &message = midiIn.midiMessages[i];
                ofxOscMessage m;
                if ( midiIn.sendRaw )
                {
                    m.setAddress( "/" + midiIn.currentDeviceName );
                    for (unsigned int j=0;j<message.bytes.size();j++ )
                    {
                        m.addCharArg(message.bytes[j]);
                    }
                }
                else if (message.status < MIDI_SYSEX)
                {
                    string address = "/" + string(midiIn.currentDeviceName) + "/" + ofToString( message.channel ) + "/" + space2underscore(message.getStatusString(message.status));
                    switch (message.status) {
                    case MIDI_NOTE_OFF:
                    case MIDI_NOTE_ON:
                        address.append( "/" + ofToString(message.pitch ) );
                        m.setAddress( address );
                        m.addIntArg( message.velocity );
                        break;
                    case MIDI_CONTROL_CHANGE:
                        address.append( "/" + ofToString(message.control ) );
                        m.setAddress( address );
                        m.addIntArg( message.value );
                        break;
                    case MIDI_PROGRAM_CHANGE:
                    case MIDI_AFTERTOUCH: // aka channel pressure
                        //m.addIntArg( message.control );
                        m.addIntArg( message.value );
                        break;
                    case MIDI_PITCH_BEND:
                        m.setAddress( address );
                        m.addIntArg( message.value );
                        break;
                    case MIDI_POLY_AFTERTOUCH: // aka key pressure
                        address.append( "/" + ofToString(message.pitch ) );
                        m.setAddress( address );
                        m.addIntArg( message.value );
                        break;
                    case MIDI_SONG_POS_POINTER:
                        m.setAddress( address );
                        m.addIntArg( message.value );
                        break;
                    default:
                        ofLogError() << "sendMidi unkown status";
                        break;
                    /* not handled
                        // system messages
                    case MIDI_SYSEX:
                    case MIDI_TIME_CODE:
                    case MIDI_SONG_POS_POINTER:
                    case MIDI_SONG_SELECT:
                    case MIDI_TUNE_REQUEST:
                    case MIDI_SYSEX_END:
                    case MIDI_TIME_CLOCK:
                    case MIDI_START:
                    case MIDI_CONTINUE:
                    case MIDI_STOP:
                    case MIDI_ACTIVE_SENSING:
                    case MIDI_SYSTEM_RESET:
                    */
                    }

                }
                bundle.addMessage(m);
            }
        }
        //check if not empty & send
        if ( bundle.getMessageCount() > 0 )
        {
            clients[i]->sendBundle(bundle);
        }
    }
    // clear captured midi messages
    midiIn.lock.lock();
    std::vector<ofxMidiMessage>::iterator itr = midiIn.midiMessages.begin();
    while (itr != midiIn.midiMessages.end() )
    {
        itr = midiIn.midiMessages.erase(itr);
    }
    midiIn.lock.unlock();

}

void ofApp::getMarkers(client *c, ofxOscBundle *bundle)
{
    if (c->getMarker())
    {
        for (int i = 0; i < natnet.getNumFilterdMarker(); i++)
        {
            ofxOscMessage m;
            m.setAddress("/marker");
            m.addIntArg(i);
            m.addFloatArg(natnet.getFilterdMarker(i).x);
            m.addFloatArg(natnet.getFilterdMarker(i).y);
            m.addFloatArg(natnet.getFilterdMarker(i).z);
            
            bundle->addMessage(m);
        }
    }
}

void ofApp::getRigidbodies(client *c, ofxOscBundle *bundle, vector<ofxNatNet::RigidBodyDescription> rbd )
{
    if ( c->getRigid() )
    {
        // we use getRigidBodyDescriptions() together with natnet.getRigidBodyAt(i)
        // because the natnet.getRigidBodyAt(i) does not have the name of th erigidbody in natnet version 2.9.0.0
        for (int i = 0; i < natnet.getNumRigidBody(); i++)
        {
            const ofxNatNet::RigidBody &RB = natnet.getRigidBodyAt(i);
            
            // Get the matirx
            ofMatrix4x4 matrix = RB.matrix;
            
            // Decompose to get the different elements
            ofVec3f position;
            ofQuaternion rotation;
            ofVec3f scale;
            ofQuaternion so;
            matrix.decompose(position, rotation, scale, so);
            
            //we're going to fetch or create this
            RigidBodyHistory *rb;
            
            //Get or create rigidbodyhistory
            bool found = false;
            for( int r = 0; r < rbHistory.size(); ++r )
            {
                if ( rbHistory[r].rigidBodyId == rbd[i].id )
                {
                    rb = &rbHistory[r];
                    found = true;
                }
            }
            
            if ( !found )
            {
                rb = new RigidBodyHistory( rbd[i].id, position, rotation );
                rbHistory.push_back(*rb);
            }
            
            //if objects are not tracked, move them to a galaxy far far away
            // this has been removed in favour of just sending an isActive to clients
            /*
            if ( !RB.isActive() ) {
                rb->framesInactive++;
                if ( rb->framesInactive > 120 ) {
                    position.x += 1000;
                    position.y += 1000;
                    position.z += 1000;
                }
            }
            else {
                rb->framesInactive = 0;
            }
             */
            
            ofVec3f velocity;
            ofVec3f angularVelocity;
            
            if ( rb->firstRun == TRUE )
            {
                rb->currentDataPoint = 0;
                rb->firstRun = FALSE;
            }
            else
            {
                if ( rb->currentDataPoint < 2 * SMOOTHING + 1 )
                {
                    rb->velocities[rb->currentDataPoint] = ( position - rb->previousPosition ) * invFPS;
                    
                    ofVec3f diff = ( rb->previousOrientation * rotation.inverse() ).getEuler();
                    rb->angularVelocities[rb->currentDataPoint] = ( diff * invFPS );
                    
                    rb->currentDataPoint++;
                }
                else
                {
                    int count = 0;
                    int maxDist = SMOOTHING;
                    ofVec3f totalVelocity;
                    ofVec3f totalAngularVelocity;
                    //calculate smoothed velocity
                    for( int x = 0; x < SMOOTHING * 2 + 1; ++x )
                    {
                        //calculate integer distance from "center"
                        //above - maxDist = influence of data point
                        int dist = abs( x - SMOOTHING );
                        int infl = ( maxDist - dist ) + 1;
                        
                        //add all
                        totalVelocity += rb->velocities[x] * infl;
                        totalAngularVelocity += rb->angularVelocities[x] * infl;
                        //count "influences"
                        count += infl;
                    }
                    
                    //divide by total data point influences
                    velocity = totalVelocity / count;
                    angularVelocity = totalAngularVelocity / count;
                    
                    for( int x = 0; x < rb->currentDataPoint - 1; ++x )
                    {
                        rb->velocities[x] = rb->velocities[x+1];
                        rb->angularVelocities[x] = rb->angularVelocities[x+1];
                    }
                    rb->velocities[rb->currentDataPoint-1] = ( position - rb->previousPosition ) * invFPS;
                    
                    ofVec3f diff = ( rb->previousOrientation * rotation.inverse() ).getEuler();
                    rb->angularVelocities[rb->currentDataPoint-1] = ( diff * invFPS );
                }
                
                rb->previousPosition = position;
                rb->previousOrientation = rotation;
            }
            
            ofxOscMessage m;
            m.addIntArg(RB.id);
            m.addStringArg(ofToString(rbd[i].name));
            m.addFloatArg(position.x);
            m.addFloatArg(position.y);
            m.addFloatArg(position.z);
            m.addFloatArg(rotation.x());
            m.addFloatArg(rotation.y());
            m.addFloatArg(rotation.z());
            m.addFloatArg(rotation.w());
            
            if ( c->getModeFlags() & ClientFlag_Velocity )
            {
                //velocity over SMOOTHING * 2 + 1 frames
                m.addFloatArg(velocity.x * 1000);
                m.addFloatArg(velocity.y * 1000);
                m.addFloatArg(velocity.z * 1000);
                //angular velocity (euler), also smoothed
                m.addFloatArg(angularVelocity.x * 1000);
                m.addFloatArg(angularVelocity.y * 1000);
                m.addFloatArg(angularVelocity.z * 1000);
            }
            
            m.addIntArg( ( RB.isActive() ? 1 : 0 ) );
            
            if ( c->getHierarchy())
                m.setAddress("/rigidBody/"+ofToString(rbd[i].name));
            else
                m.setAddress("/rigidBody");
            
            bundle->addMessage(m);
        }
    }
}

void ofApp::getSkeletons(client *c, ofxOscBundle *bundle, vector<ofxNatNet::SkeletonDescription> sd)
{
    for (int j = 0; j < natnet.getNumSkeleton(); j++)
    {
        const ofxNatNet::Skeleton &S = natnet.getSkeletonAt(j);
        vector<ofxNatNet::RigidBodyDescription> rbd = sd[j].joints;
        
        if ( c->getHierarchy())
        {
            for (int i = 0; i < S.joints.size(); i++)
            {
                const ofxNatNet::RigidBody &RB = S.joints[i];
                
                ofxOscMessage m;
                m.setAddress("/skeleton/" + ofToString(sd[j].name) + "/" +
                                ofToString(ofToString(rbd[i].name)));
                        
                // Get the matirx
                ofMatrix4x4 matrix = RB.matrix;
                
                // Decompose to get the different elements
                ofVec3f position;
                ofQuaternion rotation;
                ofVec3f scale;
                ofQuaternion so;
                matrix.decompose(position, rotation, scale, so);
                m.addStringArg(ofToString(rbd[i].name));
                m.addFloatArg(position.x);
                m.addFloatArg(position.y);
                m.addFloatArg(position.z);
                m.addFloatArg(rotation.x());
                m.addFloatArg(rotation.y());
                m.addFloatArg(rotation.z());
                m.addFloatArg(rotation.w());
                //needed for skeleton retargeting
                if ( c->getModeFlags() & ClientFlag_FullSkeleton )
                {
                    m.addIntArg(rbd[i].parent_id);
                    m.addFloatArg(rbd[i].offset.x);
                    m.addFloatArg(rbd[i].offset.y);
                    m.addFloatArg(rbd[i].offset.z);
                    //TODO: Figure out if this is needed. It's obvious for rigidbodies,
                    //          but skeletons don't have an "isActive" as a totality
                    //m.addBoolArg(RB.isActive());
                }
                
                bundle->addMessage(m);
            }
        }
        else
        {
            ofxOscMessage m;
            m.setAddress("/skeleton");
            m.addStringArg(ofToString(sd[j].name));
            m.addIntArg(S.id);
            
            for (int i = 0; i < S.joints.size(); i++)
            {
                const ofxNatNet::RigidBody &RB = S.joints[i];
                        
                // Get the matirx
                ofMatrix4x4 matrix = RB.matrix;
                
                // Decompose to get the different elements
                ofVec3f position;
                ofQuaternion rotation;
                ofVec3f scale;
                ofQuaternion so;
                matrix.decompose(position, rotation, scale, so);
                m.addStringArg(ofToString(rbd[i].name));
                m.addFloatArg(position.x);
                m.addFloatArg(position.y);
                m.addFloatArg(position.z);
                m.addFloatArg(rotation.x());
                m.addFloatArg(rotation.y());
                m.addFloatArg(rotation.z());
                m.addFloatArg(rotation.w());
                //needed for skeleton retargeting
                if ( c->getModeFlags() & ClientFlag_FullSkeleton )
                {
                    m.addIntArg(rbd[i].parent_id);
                    m.addFloatArg(rbd[i].offset.x);
                    m.addFloatArg(rbd[i].offset.y);
                    m.addFloatArg(rbd[i].offset.z);
                }
            }
            
            bundle->addMessage(m);
        }
    }
}

void ofApp::fixRanges( ofVec3f *euler )
{
    if (euler->x < -180.0f)
        euler->x += 360.0f;
    else if (euler->x > 180.0f)
        euler->x -= 360.0f;
    
    if (euler->y < -180.0f)
        euler->y += 360.0f;
    else if (euler->y > 180.0f)
        euler->y -= 360.0f;
    
    if (euler->z < -180.0f)
        euler->z += 360.0f;
    else if (euler->z > 180.0f)
        euler->z -= 360.0f;
}

void ofApp::deleteClient(int &index)
{
    ofRemoveListener(clients[index]->deleteClient, this, &ofApp::deleteClient);
    delete clients[index];
    clients.erase(clients.begin() + index); 
}


//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
    if (key == 'h'){
        visible = !visible;
    }
    if (key == 'p')
    {
        running = !running;
        //if (running) natnet.sendRequestDescription();
    }
    if (key == OF_KEY_ALT )
    {
#ifdef DEBUG
#ifdef TARGET_WIN32
        if ( IsDebuggerPresent() ) __debugbreak();
#else
        if ( gdbIsAttached() ) raise(SIGTRAP);
#endif
#endif
    }
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
    
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){
    
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
    
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h)
{
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
    
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
    
}

void ofApp::saveData(string filepath="")
{
    if ( filepath == "" ) {
        filepath = ofFilePath::join(userDataDir, save_fileName);// "setup.xml");
    }
    ofLogNotice("Starting save data to " + filepath);
    ofxXmlSettings save;
    save.addTag("setup");
    save.pushTag("setup",0);
    save.addValue("fps", FPS);
    save.addValue("interface", iface_list.at(current_iface_idx));
    save.addValue("ip", natnetip_char);
    save.popTag();
    save.addTag("midi");
    save.pushTag("midi",0);
    save.addValue("currentDevice", midiIn.currentDevice);
    save.addValue("currentDeviceName", midiIn.currentDeviceName);
    save.addValue("ignoreSysex", midiIn.ignoreSysex);
    save.addValue("ignoreTiming", midiIn.ignoreTiming);
    save.addValue("ignoreSense", midiIn.ignoreSense);
    save.addValue("verbose", midiIn.verbose);
    save.addValue("sendRaw", midiIn.sendRaw);
    save.popTag();
    save.addTag("OSCReceive");
    save.pushTag("OSCReceive",0);
    save.addValue("oscListenPort", oscListenPort);
    save.popTag();
    for (int i = 0; i < clients.size(); i++)
    {
        save.addTag("client");
        save.pushTag("client",i);
        save.addValue("ip", clients[i]->getIP());
        save.addValue("port", clients[i]->getPort());
        save.addValue("name", clients[i]->getName());
        save.addValue("rigid", clients[i]->getRigid());
        save.addValue("marker", clients[i]->getMarker());
        save.addValue("skeleton", clients[i]->getSkeleton());
        save.addValue("live", clients[i]->getLive());
        save.addValue("vrtrackers",  clients[i]->getVRTrackersFlag());
        save.addValue("hierarchy", clients[i]->getHierarchy());
        save.addValue("midi", clients[i]->getMidiFlag());
        save.addValue("mode", clients[i]->getModeFlags());
        save.addValue("midi", clients[i]->getMidiFlag());
        save.addValue("osc", clients[i]->getOSCFlag());
        save.popTag();
    }
    save.save(filepath);

    //store name of last saved file
    string persistentPath = ofFilePath::join(userDataDir, "persistent_state.xml");
    ofxXmlSettings persistent_state;
    persistent_state.addTag("name");
    persistent_state.pushTag("name", 0);
    persistent_state.addValue("filename", save_fileName);
    persistent_state.popTag();
    persistent_state.save(persistentPath);

    ofLogNotice("fps "+ofToString(FPS)+" interface " + iface_list.at(current_iface_idx) +" ip "+ofToString(natnetip_char));
    ofLogNotice("Save Data Finished");
}

//--------------------------------------------------------------
// --> VRTRackers
void ofApp::newDeviceData(ofxOpenVRTrackerEventArgs& args) {

    // Save debug info
    string tmp = "";
    for (int i = 0; i < (*args.devices->getTrackers()).size(); i++) {
        tmp += (*args.devices->getTrackers())[i]->getDebugString() + "\n";

        string name = ofToString((*args.devices->getTrackers())[i]->serialNumber);

        vector<string> trackerInfo;
        trackerInfo.push_back(ofToString((*args.devices->getTrackers())[i]->serialNumber));
        trackerInfo.push_back(ofToString((*args.devices->getTrackers())[i]->bConnected));
        trackerInfo.push_back(ofToString((*args.devices->getTrackers())[i]->batteryFraction));
        trackerInfo.push_back(ofToString((*args.devices->getTrackers())[i]->trackedIndex));

        map<string,vector<string>>::iterator it;
        it = VRTrackerInfo.find(name);
        // Update values
        if (it != VRTrackerInfo.end())
        {
            it->second = trackerInfo;
        }
        // insert values
        else{
            VRTrackerInfo.insert( std::pair<string,vector<string>>(name,trackerInfo));
        }




    }
    out = tmp;

    // update for gui
    numConnectedVRTrackers = (*args.devices->getTrackers()).size();

    // Loop through clients
    for( int i = 0; i < clients.size(); ++i )
    {
        if(clients[i]->getVRTrackersFlag()){

            ofxOscBundle bundle;
            // Loop through trackers
            for (int k = 0; k < (*args.devices->getTrackers()).size(); k++){

                // FIXME: add hiarchy toggle ?
                // send messsage per tracker if connected
                if( (*args.devices->getTrackers())[k]->bConnected == true){
                    //create OSC message
                    ofxOscMessage m;
                    m.setAddress("/vrtrackers/"+ofToString((*args.devices->getTrackers())[k]->serialNumber));

                    // name
                    //m.addStringArg(ofToString((*args.devices->getTrackers())[k]->serialNumber));

                    // position
                    m.addFloatArg((*args.devices->getTrackers())[k]->position.x);
                    m.addFloatArg((*args.devices->getTrackers())[k]->position.y);
                    m.addFloatArg((*args.devices->getTrackers())[k]->position.z);

                    //orientation
                    m.addFloatArg((*args.devices->getTrackers())[k]->quaternion.x);
                    m.addFloatArg((*args.devices->getTrackers())[k]->quaternion.y);
                    m.addFloatArg((*args.devices->getTrackers())[k]->quaternion.z);
                    m.addFloatArg((*args.devices->getTrackers())[k]->quaternion.w);

                    // velocity
                    m.addFloatArg((*args.devices->getTrackers())[k]->linearVelocity.x);
                    m.addFloatArg((*args.devices->getTrackers())[k]->linearVelocity.y);
                    m.addFloatArg((*args.devices->getTrackers())[k]->linearVelocity.z);

                    // angular velocity
                    m.addFloatArg((*args.devices->getTrackers())[k]->angularVelocity.x);
                    m.addFloatArg((*args.devices->getTrackers())[k]->angularVelocity.y);
                    m.addFloatArg((*args.devices->getTrackers())[k]->angularVelocity.z);

                    bundle.addMessage(m);
                }
            }

            //check if not empty & send
            if ( bundle.getMessageCount() > 0 )
            {
                clients[i]->sendBundle(bundle);
            }

        }

    }

}


void ofApp::exit()
{
    for (int i = 0; i < clients.size(); i++)
    {
        delete clients[i];
    }

#if defined(_WIN32) || defined(_WIN64)
    // VRTRacker stuff -----------------------------------------------------
    // Remove listener for new device data
    ofRemoveListener(openvr.newDataReceived, this, &ofApp::newDeviceData);
    // disconnect from the server
    openvr.disconnect();
#endif
   
}

static bool version_popup = false;

void ofApp::doGui() {
    this->mouseOverGui = false;
    if (this->guiVisible)
    {
        auto mainSettings = ofxImGui::Settings();
        //ui stuff
        gui.begin();
        // Create a main menu bar
        float mainmenu_height = 0;
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                //if (ImGui::MenuItem("Open CSV..", "Ctrl+O")) { loadAFile(); }
                if (ImGui::MenuItem("Save Setup", "Ctrl+S"))   {saveData(); }
                if (ImGui::MenuItem("About", "Ctrl+i")) { version_popup=true; }
                if (ImGui::MenuItem("Exit", "Ctrl+W"))  { ofExit(0); }
                ImGui::EndMenu();
            }
            mainmenu_height = ImGui::GetWindowSize().y;
            ImGui::EndMainMenuBar();
        }

        // clients window
        ImGui::SetNextWindowPos(ImVec2( 0, mainmenu_height ));
        ImGui::SetNextWindowSize(ImVec2( ofGetWidth()-351, ofGetHeight()-mainmenu_height));
        ImGui::Begin("clientspanel", NULL,  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
        
        // DRAW CLIENTS
        int ypos = 0;
        for (int i = 0; i < clients.size(); i++)
        {
            bool enabled = true;
            string name = ICON_FA_DESKTOP " " + clients[i]->getName()+"##"+clients[i]->getIP()+ofToString(clients[i]->getPort());
            if ( ImGui::CollapsingHeader(name.c_str(), &enabled, ImGuiTreeNodeFlags_DefaultOpen) )
            {
                clients[i]->doGui();
            }
            if ( ! enabled )
            {
                ofNotifyEvent(clients[i]->deleteClient,i);
            }
        }
        ImGui::End();
        
        // right dock
        ImGui::SetNextWindowPos(ImVec2( ofGetWidth()-350, mainmenu_height ));
        ImGui::SetNextWindowSize(ImVec2( 350, ofGetHeight()-mainmenu_height));
        ImGui::Begin("rightpanel", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_HorizontalScrollbar);
        
        //ImGui::PushFont(fontSubTitle);
        if ( ImGui::CollapsingHeader("Global Settings", NULL, ImGuiTreeNodeFlags_DefaultOpen) )
        {
            //ImGui::PopFont();
        
            ImGui::Combo("interface", &current_iface_idx, iface_list);
            ImGui::InputText("natnet ip", natnetip_char, 16);
            ImGui::InputInt("FPS", &FPS);

            if ( natnet.isConnected() )
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(2/7.0f, 0.6f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(3/7.0f, 0.7f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(4/7.0f, 0.8f, 0.8f));
            }
            if ( ImGui::Button(ICON_FA_PLUG " Connect") )
            {
                connectNatnet(iface_list.at(current_iface_idx), ofToString(natnetip_char));
            }
            if ( natnet.isConnected() ) ImGui::PopStyleColor(3);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            static char client_name[128] = "New Client";
            ImGui::InputText("client_name", client_name, IM_ARRAYSIZE(client_name));
            static char client_ip[16] = "127.0.0.1";
            ImGui::InputText("client_ip", client_ip, IM_ARRAYSIZE(client_ip));
            static int client_port = 6000;
            ImGui::InputInt("client port", &client_port);
            if ( ImGui::Button(ICON_FA_DESKTOP " Add Client") )
            {
                addClient(clients.size(), ofToString(client_ip), client_port, ofToString(client_name), false, false, false, true, false, false, 0, false, false);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();


            ImGui::InputText("file name", save_fileName, 32);
            if ( ImGui::Button(ICON_FA_SAVE " Save Setup") )
            {
                saveData();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FILE_DOWNLOAD " Load Setup"))
            {
                ofFileDialogResult openFileResult = ofSystemLoadDialog("Select an XML file to load");
                if ( openFileResult.bSuccess )
                {
                    setupData( openFileResult.getPath() );
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open Folder"))
            {
                browseAppConfigDir();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        if ( ImGui::CollapsingHeader("OSC Receiver Settings", NULL, ImGuiTreeNodeFlags_None) )
        {
            if ( ImGui::InputInt("OSC Listen Port", &oscListenPort) )
            {
                setupOSCReceiver();
            }
        }
        if ( ImGui::CollapsingHeader("Midi Settings", NULL, ImGuiTreeNodeFlags_None) )
        {
            if ( ImGui::Combo("Midi Device", &midiIn.currentDevice, midiIn.midiDevices) )
            {
                if ( midiIn.midiIn.isOpen() )
                {
                    midiIn.midiIn.closePort();
                }
                midiIn.midiIn.openPort( midiIn.currentDevice - 1 );
                if ( midiIn.currentDeviceName == "" )
                {
                    midiIn.currentDeviceName = space2underscore(midiIn.midiDevices[midiIn.currentDevice]);
                }
            }
            if ( ImGui::InputText("Custom name", &midiIn.currentDeviceName, ImGuiInputTextFlags_EnterReturnsTrue) )
            {
                midiIn.currentDeviceName = space2underscore(midiIn.currentDeviceName);
            }
            ImGui::Columns(2, NULL, false);

            if ( ImGui::Checkbox("Ignore Sysex", &midiIn.ignoreSysex ) )
            {
                midiIn.midiIn.ignoreTypes(midiIn.ignoreSysex, midiIn.ignoreTiming, midiIn.ignoreSense);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Ignore Midi Sysex events");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::NextColumn();
            if ( ImGui::Checkbox("Verbose", &midiIn.verbose ) )
            {
                midiIn.midiIn.setVerbose(midiIn.verbose);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Send ofxMidi log messages to the log console");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::NextColumn();

            if ( ImGui::Checkbox("Ignore Timing", &midiIn.ignoreTiming ) )
            {
                midiIn.midiIn.ignoreTypes(midiIn.ignoreSysex, midiIn.ignoreTiming, midiIn.ignoreSense);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Ignore Midi Timing events");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::NextColumn();
            ImGui::Checkbox("Send Raw", &midiIn.sendRaw );
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Send raw midi bytes");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::NextColumn();
            if ( ImGui::Checkbox("Ignore Sense", &midiIn.ignoreSense ) )
            {
                midiIn.midiIn.ignoreTypes(midiIn.ignoreSysex, midiIn.ignoreTiming, midiIn.ignoreSense);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Ignore Midi Sense events");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::Columns(1);
            if ( ImGui::Button("Refresh devices") )
            {
                // refresh available devices
                midiIn.refreshDevices();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("refresh available midi devices connected to the computer");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        // VR trackers
        if ( ImGui::CollapsingHeader("VRTrackers Settings", NULL, ImGuiTreeNodeFlags_None) ){

            ImGui::Columns(2, "VRTrackers info");
            ImGui::Text("Num. VRTrackers: "); ImGui::NextColumn();
            ImGui::Text(ofToString(numConnectedVRTrackers).c_str());
            ImGui::NextColumn();
            ImGui::Columns(1);


            string labels[4] = {"","connected:","batteryStatus:","trackedIndex:"};
            for (auto const& vrt : VRTrackerInfo){
                vector<string> info = vrt.second;


                if (ImGui::TreeNode(info[0].c_str()))
                {
                    ImGui::Columns(2);
                    for(int j=1;j<info.size();j++){
                        ImGui::Text(labels[j].c_str());
                        ImGui::NextColumn();
                        ImGui::Text(info[j].c_str());
                        ImGui::NextColumn();

                    }
                    ImGui::Columns(1);
                    ImGui::TreePop();
                }
            }


            ImGui::Columns(1);

        }
        if ( ImGui::CollapsingHeader("NatNet statistics", NULL, ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Columns(2, "natnetstats");
            ImGui::Text("frames: "); ImGui::NextColumn();
            ImGui::Text("%s",ofToString(natnet.getFrameNumber()).c_str()); ImGui::NextColumn();
            ImGui::Text("data rate: "); ImGui::NextColumn();
            ImGui::Text("%s",ofToString(natnet.getDataRate()).c_str()); ImGui::NextColumn();
            ImGui::Text("connected: "); ImGui::NextColumn();
            string con =(natnet.isConnected() ? "YES" : "NO");
            ImGui::Text("%s",con.c_str()); ImGui::NextColumn();
            ImGui::Text("num markers: "); ImGui::NextColumn();
            ImGui::Text("%s",ofToString(natnet.getNumMarker()).c_str()); ImGui::NextColumn();
            ImGui::TextWrapped("num filtererd (non rigidbodies) marker:: "); ImGui::NextColumn();
            ImGui::Text("%s",ofToString(natnet.getNumFilterdMarker()).c_str()); ImGui::NextColumn();
            ImGui::Text("num rigidbody: "); ImGui::NextColumn();
            ImGui::Text("%s",ofToString(natnet.getNumRigidBody()).c_str()); ImGui::NextColumn();
            ImGui::Text("num skeleton: "); ImGui::NextColumn();
            ImGui::Text("%s",ofToString(natnet.getNumSkeleton()).c_str()); ImGui::NextColumn();
            ImGui::Columns(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        if ( ImGui::CollapsingHeader("log console", NULL, 0 ) )
        {
            uiLogWidget.doGui();
        }
        ImGui::End();
        
        if ( version_popup )
        {
            // show the help window
            //ImGui::ShowDemoWindow(&version_popup);
            ImGui::SetNextWindowPos( ImVec2(ofGetWindowWidth()/2, ofGetWindowHeight()/2 ), ImGuiCond_FirstUseEver, ImVec2(0.5,0.5) );
            ImGui::SetNextWindowSize( ImVec2(ofGetWindowWidth()/2, ofGetWindowHeight()/2), ImGuiCond_FirstUseEver );
            ImGui::Begin("About & information", &version_popup, ImGuiWindowFlags_AlwaysUseWindowPadding );
            ImGui::Columns(2, "version_info");
            ImGui::Text("App Version:"); ImGui::NextColumn();
            ImGui::TextWrapped( VERSION ); ImGui::NextColumn();

            //FIXME: extend this with: build info (same way as app version), gebruikte libraries info, addons
            ImGui::Text("OS details:"); ImGui::NextColumn();
            string osdet = run_method("helpers", "platform_details").c_str();
            // https://www.khronos.org/opengl/wiki/OpenGL_Context
            osdet += "OpenGL information:\n";
            osdet += "Renderer: "+ofToString(glGetString(GL_RENDERER))+"\n";
            osdet += "Vendor: "+ofToString(glGetString(GL_VENDOR))+"\n";
            osdet += "Version: "+ofToString(glGetString(GL_VERSION))+"\n";
            osdet += "GLSL version: "+ofToString(glGetString(GL_SHADING_LANGUAGE_VERSION))+"\n";
            osdet += "GL extensions: "+	ofToString( glGetString(GL_EXTENSIONS))+"\n";
            ImGui::InputTextMultiline("##osdetails", (char *)osdet.c_str(), osdet.length(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 16), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
            ImGui::NextColumn();
            
            ImGui::Text("Path details:"); ImGui::NextColumn();
            string pdet = "Working directory: "+ofToString(ofFilePath::getCurrentWorkingDirectory())+"\n";
            pdet += "OpenFrameWorks version: "+ofToString(ofGetVersionInfo())+"\n";
            pdet += "Paths: "+ofToString(ofGetEnv("PATH"));
            
            ImGui::InputTextMultiline("##pathdetails", (char *)pdet.c_str(), pdet.length(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
            ImGui::NextColumn();
            
            ImGui::Text("Python details"); ImGui::NextColumn();
            string pydet = run_method("helpers", "python_details").c_str();
            ImGui::InputTextMultiline("##pydetails", (char *)pydet.c_str(), pydet.length(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 8), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
            ImGui::NextColumn();

            ImGui::End();
        }
        
        if(openModal){
            ImGui::OpenPopup("userFeedback");
            openModal = false;
        }


        // MODAL POP UP
        if (ImGui::BeginPopupModal("userFeedback", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(UserFeedback.c_str());
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120,0))) { ImGui::CloseCurrentPopup(); }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }

        gui.end();
        this->mouseOverGui = mainSettings.mouseOverGui;
    }
}




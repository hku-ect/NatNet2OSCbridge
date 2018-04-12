#include "ofApp.h"

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

//--------------------------------------------------------------
void ofApp::setup()
{
	ofSetLogLevel(OF_LOG_VERBOSE);
	
	
	ofSetVerticalSync(true);
    ofBackground(67,67,67);
    font.load("verdana.ttf", 12);
    
    setupConnectionInterface();
    setupData();
    sentRequest = false;
    visible = true;
    numRigidBody = 0;
    numSkeleton = 0;
    running = true;
    
    rigidBodySize = -1;
    skeletonSize = -1;
    connected = false;
    invFPS = 1.0f / ofToInt(fps.getText());
    FPS = ofToInt(fps.getText());
    
   
}

void ofApp::setupConnectionInterface(){
    
    // These variables position the connection interface
    InterfaceX = ofGetWidth()-350;
    InterfaceY = 50;
    
    connect.setup(ofRectangle(InterfaceX, InterfaceY+90, 80, 20), "Connect", 12, ofColor(0,0,0), ofColor(255,255,255));
    addButton.setup(ofRectangle(InterfaceX, InterfaceY+260, 80, 20), "Add User", 12, ofColor(0,0,0), ofColor(255,255,255));
    saveButton.setup(ofRectangle(InterfaceX, InterfaceY+320, 80, 20), "Save Setup", 12, ofColor(0,0,0), ofColor(255,255,255));
    
    interfaceName.setup(ofRectangle(InterfaceX, InterfaceY, 140, 20), 10, "en0","Interface");
    interfaceIP.setup(ofRectangle(InterfaceX, InterfaceY+30, 140, 20), 10, "127.0.0.1","Natnet IP");
    fps.setup(ofRectangle(InterfaceX, InterfaceY+60, 140, 20), 10, "30","FPS");
	
    newName.setup(ofRectangle(InterfaceX, InterfaceY+170, 140, 20), 10, "New Client","Client Name");
    newIP.setup(ofRectangle(InterfaceX, InterfaceY+200, 140, 20), 10, "127.0.0.1","Client IP");
    newPort.setup(ofRectangle(InterfaceX, InterfaceY+230, 140, 20), 10, "6200","Client Port");

}

void ofApp::setupData()
{
	ofLogVerbose("SetupData:: connection");
	ofxXmlSettings data("setup.xml");
    data.pushTag("setup",0);
    int fRate = data.getValue("fps", 30);
	ofLogWarning("setupData :: fps: " + ofToString(fRate));
    string interface = data.getValue("interface", "en0");
	ofLogWarning("setupData :: interface: " + interface);
    string natnetip = data.getValue("ip", "10.200.200.13");
	ofLogWarning("setupData :: natnetip: " + natnetip);
    interfaceName.setText(interface);
    interfaceIP.setText(natnetip);
    fps.setText(ofToString(fRate));
    

	

    ofSetFrameRate(fRate);
    data.popTag();

	ofLogVerbose("SetupData:: clients");

    int numClients = data.getNumTags("client");
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
        ClientMode mode = (ClientMode)data.getValue("mode", 0);
        addClient(i,ip,port,name,r,m,s,live,hier,mode);
        data.popTag();
    }

	ofLogVerbose("SetupData :: DONE");
}


bool ofApp::connectNatnet()
{
    natnet.setup(interfaceName.getText(), interfaceIP.getText());  // interface name, server ip
    if(natnet.isConnected())
    {
        natnet.setScale(100);
        natnet.setDuplicatedPointRemovalDistance(20);
        natnet.sendRequestDescription();
    }
	return false;
}


//--------------------------------------------------------------
void ofApp::update()
{
    if(running && natnet.isConnected())
    {
        // get Natnet info for changes
        if(sentRequest == false || ofGetFrameNum()%FPS == 1){
            natnet.sendRequestDescription();
            sentRequest = true;
        }
        natnet.update();
        sendOSC();
    }
    if(natnet.isConnected()) connected = true;
    else connected = false;
}

//--------------------------------------------------------------
void ofApp::draw()
{
    string msg;
    for (int i = 0; i < clients.size(); i++)
    {
        clients[i]->draw();
    }
    
    if (visible)
    {
        ofSetColor(255, 255, 255);
        font.drawString("Global Settings", InterfaceX, InterfaceY-20);
        interfaceName.draw();
        interfaceIP.draw();
        fps.draw();
        connect.draw();
        if (connected) ofSetColor(0, 255, 0);
        else ofSetColor(255, 0, 0);
        ofDrawCircle(InterfaceX+100, 150, 10);
        

        ofSetColor(255, 255, 255);

        font.drawString("New User", InterfaceX, InterfaceY+150);
        
        newName.draw();
        newIP.draw();
        newPort.draw();
        
        addButton.draw();
        
        saveButton.draw();
        
        ofSetColor(255, 255, 255);
        font.drawString("Informations", InterfaceX, InterfaceY+370);
        
        string info;
        info += "natnet tracking informations: \n";
        info += "frames: " + ofToString(natnet.getFrameNumber()) + "\n";
        info += "data rate: " + ofToString(natnet.getDataRate()) + "\n";
        info += string("connected: ") + (natnet.isConnected() ? "YES" : "NO") + "\n";
        info += "num markers set: " + ofToString(natnet.getNumMarkersSet()) + "\n";
        info += "num marker: " + ofToString(natnet.getNumMarker()) + "\n";
        info += "num filterd (non regidbodies) marker: " +
        ofToString(natnet.getNumFilterdMarker()) + "\n";
        info += "num rigidbody: " + ofToString(natnet.getNumRigidBody()) + "\n";
        info += "num skeleton: " + ofToString(natnet.getNumSkeleton()) + "\n";
        info += string("press p to pause clients, is paused: ") + (running ? "NO" : "YES") + "\n";
        info += "press h to hide the informations \n";
        
        ofSetColor(255);
        ofDrawBitmapString(info, InterfaceX, InterfaceY+390);
        
    }
}

void ofApp::addClient(int i,string ip,int p,string n,bool r,bool m,bool s, bool live, bool hierarchy, ClientMode mode)
{
    client *c = new client(i,ip,p,n,r,m,s,live, hierarchy, mode);
    ofAddListener(c->deleteClient, this, &ofApp::deleteClient);
    clients.push_back(c);
}

void ofApp::sendOSC()
{
    for( int i = 0; i < clients.size(); ++i )
    {
        ofxOscBundle bundle;
        
        //markers
        if ( clients[i]->getMarker() )
            getMarkers( clients[i], &bundle );
        
        //rigidbodies
        if ( clients[i]->getRigid() )
            getRigidbodies( clients[i], &bundle);
        
        //skeletons
        if ( clients[i]->getSkeleton() )
            getSkeletons( clients[i], &bundle);
        
        //check if not empty & send
        if ( bundle.getMessageCount() > 0 )
        {
            clients[i]->sendBundle(bundle);
        }
    }
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

void ofApp::getRigidbodies(client *c, ofxOscBundle *bundle)
{
    if ( c->getRigid() )
    {
        vector<ofxNatNet::RigidBodyDescription> rbd = natnet.getRigidBodyDescriptions();
        
        // we use getRigidBodyDescriptions() together with natnet.getRigidBodyAt(i)
        // because the natnet.getRigidBodyAt(i) does not have the name of th erigidbody in natnet version 2.9.0.0
        // We use the rbd.size() to loop and not the natnet.getNumRigidBody()
        if ( natnet.getNumRigidBody() != rbd.size() ) {

            ofLogError() << "Race condition ahead!!! getNumRigidBody() != rdb.size. Skiping getRigidBodies request";
            return;
        }

        for (int i = 0; i < rbd.size(); i++)
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
            
            if ( c->getMode() != ClientMode_GearVR )
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

void ofApp::getSkeletons(client *c, ofxOscBundle *bundle)
{
    vector<ofxNatNet::SkeletonDescription> sd = natnet.getSkeletonDescriptions();
    if ( ! sd.size() )
    {
        ofLogError() << "No SkeletonDesctiptions found!!!";
        return;
    }

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
                if ( c->getMode() == ClientMode_FullSkeleton )
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
                if ( c->getMode() == ClientMode_FullSkeleton )
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

void ofApp::deactivateInputs()
{
    //deactivate all inputfields
    interfaceName.deactivate();
    interfaceIP.deactivate();
    fps.deactivate();
    newName.deactivate();
    newIP.deactivate();
    newPort.deactivate();
}

void ofApp::deleteClient(int &index)
{
    ofRemoveListener(clients[index]->deleteClient, this, &ofApp::deleteClient);
    delete clients[index];
    clients.erase(clients.begin() + index);
    for (int i = 0; i < clients.size(); i++)
    {
        clients[i]->rearangePosition(i,true);
    }    
}


//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
    //first check the input fields
    if (interfaceName.getState())
    {
        interfaceName.addKey(key);
        return;
    }
    if (interfaceIP.getState())
    {
        interfaceIP.addKey(key);
        return;
    }
    if (fps.getState())
    {
        fps.addKey(key);
        return;
    }
    if (newName.getState())
    {
        newName.addKey(key);
        return;
    }
    if (newIP.getState())
    {
        newIP.addKey(key);
        return;
    }
    if (newPort.getState())
    {
        newPort.addKey(key);
        return;
    }
    
    if (key == 'h'){
        visible = !visible;
        
        for( int i = 0; i < clients.size(); ++i )
        {
            // visible == notWholescreen -> if interface is visible do not use whole screen for clients
            clients[i]->rearangePosition(i,visible);
        }
    }
    if (key == 'p')
    {
        running = !running;
        if (running) natnet.sendRequestDescription();
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
    deactivateInputs();
    for (int i = 0; i < clients.size(); i++)
    {
        bool isInside = clients[i]->getArea().inside(x, y);
        if (isInside)
        {
            clients[i]->isInside(x, y);
            return;
        }
    }
    if(interfaceName.isInside(x, y)) return;
    if(interfaceIP.isInside(x, y)) return;
    if(fps.isInside(x, y)) return;
    if(newName.isInside(x, y)) return;
    if(newIP.isInside(x, y)) return;
    if(newPort.isInside(x, y)) return;
    if(addButton.isInside(x, y))
    {
        addClient(clients.size(), newIP.getText(), ofToInt(newPort.getText()), newName.getText(), false, false, false, true, false, ClientMode_Default);
        return;
    }
    if(saveButton.isInside(x, y)) saveData();
    if (connect.isInside(x, y)) connectNatnet();
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
    
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
    
    // Reposition clients when screen is resized
    ofLogNotice("Window resized");
    for( int i = 0; i < clients.size(); ++i )
    {
        clients[i]->rearangePosition(i,true);
    }
    
    
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
    
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
    
}

void ofApp::saveData()
{
    ofxXmlSettings save;
    save.addTag("setup");
    save.pushTag("setup",0);
    save.addValue("fps", ofToInt(fps.getText()));
    save.addValue("interface", interfaceName.getText());
    save.addValue("ip", interfaceIP.getText());
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
        save.addValue("hierarchy", clients[i]->getHierarchy());
        save.addValue("mode", clients[i]->getMode());
        save.popTag();
    }
    save.save("setup.xml");
}


void ofApp::exit()
{
    for (int i = 0; i < clients.size(); i++)
    {
        delete clients[i];
    }
}


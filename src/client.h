//
//  client.h
//  NatNet2OSCbridge
//
//  Created by Enrico Becker on 12.10.15.
//
//

#ifndef __NatNet2OSCbridge__client__
#define __NatNet2OSCbridge__client__

#include <stdio.h>
#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxImGui.h"

//These function as bit flags
enum ClientFlags
{
    ClientFlag_Velocity     = 1,
    ClientFlag_FullSkeleton = 2
    //4 -> this could become ClientFlag_Hierarchy
    //8
    //16
    //32
};

class client
{
public:
    client(int ind,string i,int p,string n,bool r,bool m,bool s, bool live, bool vrt, bool hier, int modeFlags, bool midi, bool osc);
    ~client();
    
    void setupSender();
    void sendData(ofxOscMessage &m);
    void sendBundle(ofxOscBundle &b);
    
    void setRigid(bool value);
    void setMarker(bool value);
    void setSkeleton(bool value);
    void doGui();
    
    int &getID();
    string &getIP();
    int &getPort();
    string &getName();
    bool &getRigid();
    bool &getMarker();
    bool &getSkeleton();
    bool &getLive();
    bool &getHierarchy();
    bool &getMidiFlag() { return recvMidi; }
    bool &getOSCFlag() { return recvOSC; }
    bool &getVRTrackersFlag() { return recvVRTrackers; }
    int &getModeFlags();

    ofEvent<int> deleteClient;
    
private:
    int             index;
    string          ip;
    int             port;
    string          name;
    bool            isRigid;
    bool            isMarker;
    bool            isSkeleton;
    bool            isLive;
    bool            deepHierarchy;
    bool            recvMidi = false;
    bool            recvOSC = false;
    bool            recvVRTrackers = false;
    int             modeFlags;
    ofxOscSender    sender;

    string          rigidstr;
    string          markstr;
    string          skelstr;
    string          hierstr;

    vector<pair<string,string>>   clientMode_list =
    {
        { "Vel", "Send velocity of rigidbodies" },
        { "Full", "Send full skeleton data" }
    };
};

#endif /* defined(__NatNet2OSCbridge__client__) */

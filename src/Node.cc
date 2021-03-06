#include "Node.h"

Define_Module(Node);

unsigned char Node::parityBits(const char *string)
{

    unsigned char checkBits = 0;
    for (int i = 0; i < strlen(string); i++)
    {
        checkBits = checkBits ^ (unsigned char)string[i];
    }
    return checkBits;
}

bool Node::checkError(const char *string, const bits &checkBits)
{
    unsigned char checkChar = parityBits(string);
    unsigned long i = checkBits.to_ulong();
    unsigned char c = static_cast<unsigned char>(i);
    return c == checkChar;
}

std::string Node::randString(){
    int MAX_MSG_SIZE = 10;
    int ASCII_START = 97; // start at 65 for more range
    int ASCII_END = 122; // end at 128 for including symbols
    int msgSize = uniform(0,MAX_MSG_SIZE) + 3;
    std::string msg = "";
    for(int i=0;i<msgSize;++i){
        msg += (char)(int)uniform(ASCII_START,ASCII_END);
    }
    return msg;
}

std::vector<std::string> Node::split (const std::string &s) {
    std::vector<std::string> result;
    char delim = ' ';
    std::stringstream ss (s);
    std::string item;
    while (getline (ss, item, delim)) {
        result.push_back (item);
    }
    return result;
}

std::string Node::join(std::vector<std::string> vec){
    std::string joined = "";
    for(int i=0;i<vec.size(); i++){
        joined += vec[i];
        if(i != vec.size()-1){
            joined += " ";
        }
    }
    return joined;
}

void Node::organize(){
    int n = getParentModule()->par("n").intValue();
    std::vector<int> temp;
    for (int i=0;i<n;++i){
        temp.push_back(i);
    }
    std::vector<std::string> vec;
    for (int i=0;i<n;++i){
        int pick = (int)(uniform(0,temp.size()) + rand() + (int)simTime().dbl()) % temp.size();
        double Pstay = uniform(0, 1);
        if (temp[pick] == 0 || Pstay > 0.5)
            vec.push_back(std::to_string(temp[pick]));
        temp.erase(temp.begin() + pick, temp.begin() + pick + 1);
    }
    if(vec.size() == 1)
        vec.push_back(std::to_string((int)uniform(1,n)));
    else if(vec.size() % 2 == 1){
        if(vec[0] != "0")
            vec.erase(vec.begin(),vec.begin()+1);
        else
            vec.erase(vec.begin()+1,vec.begin()+2);
    }
    getParentModule()->par("workingCount").setIntValue(vec.size());
    std::string peers = join(vec);
    getParentModule()->par("peers").setStringValue(peers);
}

void Node::findMyPeer(){
    peerIndex = -1;
    int n = getParentModule()->par("workingCount").intValue();
    std::string peersStr = getParentModule()->par("peers").stringValue();
    std::vector<std::string> peers = split(peersStr);
    for(int i=0;i<n-1;i+=2){
        if(peers[i] == std::to_string(getIndex())){
            peerIndex = std::atoi(peers[i+1].c_str());
        }
        else if(peers[i+1] == std::to_string(getIndex())){
            peerIndex = std::atoi(peers[i].c_str());
        }
    }
}

void Node::initialize()
{
    if (getIndex()==0){
        organize();
        if(generatedCount == 0)
            scheduleAt(simTime() + par("STATS_INTERVAL").doubleValue(), new cMessage("statsGeneral"));
    }
    if (generatedCount==0){
        readMessagesFile();
    }
    scheduleAt(simTime() + par("REINIT_INTERVAL").doubleValue(), new cMessage("reinitialize"));
    findMyPeer();
    if (peerIndex != -1)
    EV<<getIndex()<<" is connected to "<<peerIndex<<endl;
    peerIndex = peerIndex > getIndex() ? peerIndex-1 : peerIndex;
    if (peerIndex == -1) return;
    double interval = uniform(0,par("NETWORK_INTERVAL").doubleValue());
    scheduleAt(simTime() + interval, new cMessage("network"));
//    if(generatedCount == 0)
//        scheduleAt(simTime() + 3, new cMessage("stats"));
    windowSize = (1 << (int)par("m")) - 1;
    nextFrameToSend = 0;
    ackExpected = 0;
    framExpected = 0;
    nBuffered = 0;
    fileIterator = 0;
    iTerminate = false;
    buffer.clear();
    this->clearTimeoutEvents();

}

void Node::handleMessage(cMessage *msg)
{

    if(msg->isSelfMessage() && !(strcmp(msg->getName(), "reinitialize"))){
        EV<<"-------- Re-initialization ---------"<<endl;
        EV<<"Node: "<<getIndex() <<" reinitialize"<<endl;
        EV<<"------------------------------------"<<endl;
        this->initialize();
    }

    if (msg->isSelfMessage() && !(strcmp(msg->getName(),"stats")))
    {
        printStatistics();
        return ;
    }
    if (msg->isSelfMessage() && !(strcmp(msg->getName(),"statsGeneral")))
        {
            printStatisticsGeneral();
            return ;
        }
    if((iTerminate && fileIterator%(windowSize+1) == ackExpected) || peerIndex == -1){
        return;
    }
    if (msg->isSelfMessage())
    {
        if(!(strcmp(msg->getName(),"network")) && buffer.size() < windowSize && !iTerminate){
            std::string s = messages[fileIterator];
            s = byteStuffing(s);
            buffer.push_back(s);
            fileIterator++;
            generatedCount++;
            int temp = getParentModule()->par("generatedCount").intValue();
            getParentModule()->par("generatedCount").setIntValue(temp+1);
            iTerminate = fileIterator == messages.size();
            MyMessage_Base *sendMsg = makeMessage(s, par("MODIFIABLE").boolValue(), false);
            sendData(sendMsg, peerIndex, par("DELAYABLE").boolValue(), par("LOSSABLE").boolValue(), par("DUPLICTABLE").boolValue());
            increment(nextFrameToSend);
            printState("sending",messages[fileIterator-1]);
        }
        else if(!(strcmp(msg->getName(), "timeout"))){
            nextFrameToSend = ackExpected;
            EV<<"time out for message :"+buffer[0] + ", frame number is "<< ackExpected <<endl;
            //EV << getIndex() <<" timeout "<<"endl;
            for(int i=0; i<buffer.size(); ++i){
                retransmittedCount++;
                int temp = getParentModule()->par("retransmittedCount").intValue();
                getParentModule()->par("retransmittedCount").setIntValue(temp+1);
                MyMessage_Base *sendMsg = makeMessage(buffer[i], par("MODIFIABLE").boolValue(), false);
                sendData(sendMsg, peerIndex, par("DELAYABLE").boolValue(), par("LOSSABLE").boolValue(), par("DUPLICTABLE").boolValue());
                increment(nextFrameToSend);
                printState("retransmission",buffer[i]);
            }
        }
    }
    else {
        MyMessage_Base *receivedMsg = check_and_cast<MyMessage_Base *>(msg);
        // checkError i.e. (if it error free) if there is no error acknowledge it else discard it
        bool isErrorFree = checkError(receivedMsg->getMPayload(), receivedMsg->getCheckBits());
        std::string payload = byteDestuffing(receivedMsg->getMPayload());
        if (isErrorFree && framExpected == receivedMsg->getSeqNum()){
            increment(framExpected);
            while(between(ackExpected,receivedMsg->getAck(),nextFrameToSend)){
                usefulSentCount++;
                int temp = getParentModule()->par("usefulSentCount").intValue();
                getParentModule()->par("usefulSentCount").setIntValue(temp+1);
                buffer.erase(buffer.begin(),buffer.begin()+1);
                if (timers[ackExpected] != nullptr){
                    cancelAndDelete(timers[ackExpected]);
                    timers[ackExpected] = nullptr;
                }
                nBuffered--;
                increment(ackExpected);
            }
            printState("receiving",payload);
        }
        else if (receivedMsg->getMType() == 0 && !isErrorFree) {
            printState("error detected",payload);
        }
        else if ((receivedMsg->getMType() == 0 && isErrorFree)){
            printState("discarded.",payload);
        }
        if (receivedMsg->getMType() == 1){
            iTerminate = true;
            ackExpected = fileIterator%(windowSize+1);
            int temp = getParentModule()->par("terminateCount").intValue();
            getParentModule()->par("terminateCount").setIntValue(temp + 1);
        }
        else if(iTerminate && fileIterator%(windowSize+1) == ackExpected) {
            EV<<" Node no. "<< getIndex() <<" terminated, and its peer node no. "<<peerIndex<<endl;
            clearTimeoutEvents();
            MyMessage_Base *sendMsg = makeMessage("end", false, true);
            send(sendMsg,"outs",peerIndex);
            int temp = getParentModule()->par("terminateCount").intValue();
            getParentModule()->par("terminateCount").setIntValue(temp + 1);
            return;
        }
    }

    if(nBuffered < windowSize){
        nBuffered++;
        double interval = uniform(0,par("NETWORK_INTERVAL").doubleValue());
        scheduleAt(simTime() + interval, new cMessage("network"));
    }
}
void Node::printState(std::string state,std::string msg){
    EV<<"-----------"<<state<<"-----------------"<<endl;
    EV<<"Node: "<< getIndex() <<" "<<state<<" payload: '"<<msg<<"' ,"<<endl;
    EV<<"S: "<< nextFrameToSend<<", ";
    EV<<"Sf: "<< ackExpected<<", ";
    EV<<"R: "<< framExpected<<", ";
    EV<<"buffered: "<< buffer.size()<<","<<endl;
    EV<<"---------------------------------"<<endl;
}

MyMessage_Base* Node::makeMessage(std::string s, bool modifiable=false, bool isLastMessage=false){
    MyMessage_Base *msg = new MyMessage_Base(s.c_str());
    msg->setSeqNum(nextFrameToSend);
    msg->setAck((framExpected+windowSize)%(windowSize+1));
    msg->setCheckBits(std::bitset<8>(parityBits(s.c_str())));
    modification(s, modifiable);
    msg->setMPayload(s.c_str());
    msg->setMType(0);
    if (isLastMessage)
        msg->setMType(1);
    if (timers[nextFrameToSend] != nullptr){
        cancelAndDelete(timers[nextFrameToSend]);
        timers[nextFrameToSend] = nullptr;
    }
    timers[nextFrameToSend] = new cMessage("timeout");
    scheduleAt(simTime() + par("TIMEOUT_INTERVAL").doubleValue(), timers[nextFrameToSend]);
    return msg;
}

void Node::increment(int &a){
    // circular(x-1) =(x>0) ? (x--) : MAX_SEQ
    a = (a+1) % (windowSize+1);
    return;
}

bool Node::between(int a,int b,int c){
 // return true if a<=b<c circular; false otherwise
    return (((a<=b)&&(b<c)) || ((c<a)&&(a<=b)) || ((b<c) && (c<a)));
}

void Node::sendData(MyMessage_Base *msg, int dest, bool delayable, bool lossable, bool duplictable){
    //first check whether to send or not  (loss)
    double rand =  uniform(0, 1) * 10;

    if(rand< par("lossRand").doubleValue() && lossable){
            EV << "Message lost " << endl;
            droppedCount++;
            int temp = getParentModule()->par("droppedCount").intValue();
            getParentModule()->par("droppedCount").setIntValue(temp+1);
            return; //don't send anything

    }

    //(duplicate)
    bool dup = false;
    rand = uniform(0, 1) * 10;
    if((rand< par("duplicateRand").doubleValue()) && duplictable){
        EV << "Duplicate happened " << endl;
        dup = true;
    }

    int delayRand = uniform(0, 1) * 10;
    if (delayRand < par("delayRand").doubleValue() && delayable)
    {
        EV << "delaying message with "<< par("TIMEOUT_INTERVAL").doubleValue() + 0.1 << " seconds " << endl;
        sendDelayed(msg, par("TIMEOUT_INTERVAL").doubleValue() + 0.1, "outs", dest);
        if(dup) sendDelayed(msg->dup(), par("TIMEOUT_INTERVAL").doubleValue() + 0.1 , "outs", dest);
    }
    else
    {
        send(msg, "outs", dest);
        if(dup) send(msg->dup(), "outs", dest);
    }
}

bool Node::modification(std::string &mypayload, bool modifiable){
    int modificationRand = uniform(0, 1) * 10;
    if (modificationRand < par("modificationRand").doubleValue() && modifiable)
    {
		droppedCount++;
        int temp = getParentModule()->par("droppedCount").intValue();
        getParentModule()->par("droppedCount").setIntValue(temp+1);
        int randBit = uniform(0, 7); // random bit in a char
        unsigned char oneBitRandom = std::pow(2, randBit);
        int randByte = uniform(0, mypayload.length()); // random char
        EV<<"Before modification '"<<mypayload<<"'"<<endl;
        mypayload[randByte] = (unsigned char)mypayload[randByte] ^ oneBitRandom;
        EV << "modifying message, modified bit = " << std::to_string(randBit) << ", modified char = " << std::to_string(randByte) << endl;
        EV<<"After  modification '"<<mypayload<<"'"<<endl;
        return true;
    }
    return false;
}

void Node::readMessagesFile(){
    std::ifstream myReadFile(std::to_string(getIndex())+".txt");
    std::string msg;
    while (getline (myReadFile, msg)) {
        messages.push_back(msg);
    }
    myReadFile.close();
}

void Node::clearTimeoutEvents(){
    for (auto & t : timers){
        if (timers[t.first] != nullptr){
            cancelAndDelete(timers[t.first]);
            timers[t.first] = nullptr;
        }
    }
    return;
}

std::string Node::byteStuffing(std::string s)
{
    EV<<"Before byte stuffing '"<<s<<"' "<<endl;
    std::string result = "";
    char flag = 'f';
    char escape = 'e';

    for(int i=0;i<s.length();i++)
    {
        if(s[i]==flag||s[i]==escape)
            result+=escape;
        result+=s[i];
    }
    EV<<"After  byte stuffing '"<<result<<"' "<<endl;
    return result;
}

std::string Node::byteDestuffing(std::string s)
{
    EV<<"Before byte de-stuffing '"<<s<<"' "<<endl;
    std::string result = "";
    char escape = 'e';

    for(int i=0;i<s.length();i++)
    {
        if(s[i]==escape)
            i++;
        if(i<s.length())
            result+=s[i];
    }
    EV<<"After  byte de-stuffing '"<<result<<"' "<<endl;
    return result;
}

void Node::printStatistics(){
    EV<<"-----------statistics-----------------"<<endl;
    EV<<"Node: "<< getIndex() <<","<<endl;
    EV<<"generated frames count: "<< generatedCount<<", "<<endl;
    EV<<"dropped frames count: "<< droppedCount<<", "<<endl;
    EV<<"useful frames count: "<< usefulSentCount<<", "<<endl;
    EV<<"retransmission frames count: "<< retransmittedCount<<", "<<endl;
    if (usefulSentCount != 0)
        EV<<"useful data transmitted %: "<< ((1.0*usefulSentCount)/(usefulSentCount+retransmittedCount))*100<<", "<<endl;
    else EV<<"useful data transmitted %: "<< 0 <<", "<<endl;
    EV<<"---------------------------------"<<endl;
    if(!iTerminate || fileIterator%(windowSize+1) != ackExpected){
        scheduleAt(simTime() + par("STATS_INTERVAL").doubleValue(), new cMessage("stats"));
    }

}
void Node::printStatisticsGeneral(){
    EV<<"-----------statistics all nodes-----------------"<<endl;
    EV<<"generated frames count: "<< getParentModule()->par("generatedCount").intValue()<<", "<<endl;
    EV<<"dropped frames count: "<< getParentModule()->par("droppedCount").intValue()<<", "<<endl;
    EV<<"useful frames count: "<< getParentModule()->par("usefulSentCount").intValue()<<", "<<endl;
    EV<<"retransmission frames count: "<< getParentModule()->par("retransmittedCount").intValue()<<", "<<endl;
    if (getParentModule()->par("usefulSentCount").intValue() != 0)
        EV<<"useful data transmitted %: "<< ((1.0*getParentModule()->par("usefulSentCount").intValue())/(getParentModule()->par("usefulSentCount").intValue()+getParentModule()->par("retransmittedCount").intValue()))*100<<", "<<endl;
    else EV<<"useful data transmitted %: "<< 0 <<", "<<endl;
    EV<<"---------------------------------"<<endl;
    int n = getParentModule()->par("workingCount").intValue();
    scheduleAt(simTime() + par("STATS_INTERVAL").doubleValue(), new cMessage("statsGeneral"));
}


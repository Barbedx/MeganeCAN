#include "DisplayManager.h"
#include "CanUtils.h"

DisplayManager::DisplayManager() {
    sessionStarted = false;
    lastPingTime = 0;
}

void DisplayManager::registerDisplay() {
    uint8_t msg[8] = {0x70, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x1B1, msg);
}

void DisplayManager::initDisplay() {
    uint8_t msg[8] = {0x70, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x121, msg);
}

void DisplayManager::enableDisplay() {
    uint8_t msg[8] = {0x04, 0x52, 0x02, 0xFF, 0xFF, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x1B1, msg);
}

void DisplayManager::startSync() {
    uint8_t startSyncMsg[8] = {0x7A, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x3DF, startSyncMsg);
}

void DisplayManager::syncOK() {
    uint8_t pingMsg[8] = {0x79, 0x00, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x3DF, pingMsg, 8);
}

void DisplayManager::syncDisp() {
    uint8_t syncDispMsg[8] = {0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01};
    CanUtils::sendMsgBuf(0x3DF, syncDispMsg);
}
 
 

void DisplayManager::messageWelcome() {
    uint8_t msg11[8] = {0x10, 0x0D, '~', 'q', 0x01, 'W', 'E', 'L'};  // 0x0D = 13 bytes
    uint8_t msg12[8] = {0x21, 'C', 'O', 'M', 'E', '!', 0x00, 0x81};
    CanUtils::sendMsgBuf(0x121, msg11);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg12);
    delay(1); 
}
void DisplayManager::messageTest() { //goood
    uint8_t msg11[8] = {0x10, 0x19, 0x7E, 0x70, 0x01, '1','2','3'};//123
    uint8_t msg12[8] = {0x21, '4', '5', '6', '7', '8', 0x10, 'x'};
    uint8_t msg13[8] = {0x22, 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    uint8_t msg14[8] = {0x23, 'x', 'x', 'x', 'x', 0x00, 0x81, 0x81};

    CanUtils::sendMsgBuf(0x121, msg11);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg12);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg13);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg14);
    delay(1); 
} 

void DisplayManager::messageTest2() {////goood
    uint8_t msg11[8] = {0x10, 0x19, 0x7E, 0x70, /*static bit -> */0x01, '1','2','3'};
    uint8_t msg12[8] = {0x21, 'Y', 'Y', 'Y', '7', '8', 0x10, 'x'};
    uint8_t msg13[8] = {0x22, 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    uint8_t msg14[8] = {0x23, 'x', 'x', 'x', 'x', 0x00, 0x81, 0x81};

    CanUtils::sendMsgBuf(0x121, msg11);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg12);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg13);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg14);
    delay(1); 
} 

void DisplayManager::messageTest3() {
    uint8_t msg11[8] = {0x10, 0x19, 0x7E,/**no lower 69 */ 0x78, 0x01, '3', '3', '3'}; //works, shows8 in chanel(0x78)
    uint8_t msg12[8] = {0x21, '3', '3', '3', '7', '8', 0x10, 'x'};
    uint8_t msg13[8] = {0x22, 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    uint8_t msg14[8] = {0x23, 'x', 'x', 'x', 'x', 0x00, 0x81, 0x81};

    CanUtils::sendMsgBuf(0x121, msg11);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg12);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg13);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg14);
    delay(1); 
} 
void DisplayManager::messageTest4() {
    uint8_t msg11[8] = {0x10, 0x19, 0x7E,/** (value 69 < 79 <*/ 0x79, 0x01, '3', '3', '3'};  
    uint8_t msg12[8] = {0x21, '3', '3', '3', '7', '8', 0x10, 'x'};
    uint8_t msg13[8] = {0x22, 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    uint8_t msg14[8] = {0x23, 'x', 'x', 'x', 'x', 0x00, 0x81, 0x81};

    CanUtils::sendMsgBuf(0x121, msg11);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg12);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg13);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg14);
    delay(1); 
} 
void DisplayManager::messageTest5() {
    uint8_t msg11[8] = {0x10, 0x19, 0x7E,/* (value 69 < 79 <*/ 0x7A ,/* 7A is max and NULL  */ 0x01/*const*/, '3', '3', '3'};  
    uint8_t msg12[8] = {0x21, '3', '3', '3', '7', '8', 0x10, 'x'};
    uint8_t msg13[8] = {0x22, 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    uint8_t msg14[8] = {0x23, 'x', 'x', 'x', 'x', 0x00, 0x81, 0x81};

    CanUtils::sendMsgBuf(0x121, msg11);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg12);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg13);
    delay(1);
    CanUtils::sendMsgBuf(0x121, msg14);
    delay(1); 
} 

bool DisplayManager::isSessionStarted() const {
    return sessionStarted;
}
 
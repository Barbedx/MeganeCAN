#include "MyELMManager.h"

MyELMManager::MyELMManager(IDisplay &display) 
    : display(display)
{
}const MyELMManager::QueryData MyELMManager::queryList[] = {
    {"22240D", "Intake Air Temp", [](int a, int b) {
         return (1.0f * ((a * 256 + b) - 2730)) / 10.0f;
    }}
};

#pragma once
#include <cstddef>
#include <cmath>
#include <cJSON.h>
namespace web_safety {
template<class Receiver>
int ReadBody(size_t length,char* buffer,size_t capacity,Receiver receive){
    if(!buffer||length==0||length>=capacity)return -1;
    size_t used=0;
    while(used<length){int n=receive(buffer+used,length-used);
        if(n<=0||static_cast<size_t>(n)>length-used){buffer[0]='\0';return -1;}
        used+=static_cast<size_t>(n);}
    buffer[used]='\0';return static_cast<int>(used);
}
inline bool ReadInt(const cJSON* root,const char* key,int minimum,int maximum,int& value){
    const cJSON* field=cJSON_GetObjectItemCaseSensitive(root,key);
    if(!cJSON_IsNumber(field)||!std::isfinite(field->valuedouble)||
       field->valuedouble<minimum||field->valuedouble>maximum||std::floor(field->valuedouble)!=field->valuedouble)return false;
    value=static_cast<int>(field->valuedouble);return true;
}
}

#include "../main/web_server/web_safety.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
static int checks=0;
#define CHECK(c) do{++checks;if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
int main(){
    char buffer[8];int calls=0;
    CHECK(web_safety::ReadBody(6,buffer,sizeof(buffer),[&](char* out,size_t n){++calls;const char* p=calls==1?"abc":"def";size_t size=n>3?3:n;memcpy(out,p,size);return static_cast<int>(size);})==6);
    CHECK(calls==2&&std::strcmp(buffer,"abcdef")==0);
    calls=0;CHECK(web_safety::ReadBody(8,buffer,sizeof(buffer),[&](char*,size_t){++calls;return 0;})<0);CHECK(calls==0);
    CHECK(web_safety::ReadBody(3,buffer,sizeof(buffer),[](char*,size_t){return -1;})<0);
    CHECK(web_safety::ReadBody(0,buffer,sizeof(buffer),[](char*,size_t){return 0;})<0);
    cJSON* root=cJSON_Parse("{\"good\":42,\"fraction\":0.5,\"large\":1e40,\"text\":\"40\",\"null\":null,\"negative\":-1}");int value=-1;
    CHECK(web_safety::ReadInt(root,"good",0,100,value)&&value==42);
    for(const char* name:{"missing","fraction","large","text","null","negative"})CHECK(!web_safety::ReadInt(root,name,0,100,value));
    CHECK(!web_safety::ReadInt(nullptr,"good",0,100,value));cJSON_Delete(root);
    std::printf("PASS: %d HTTP body/parameter safety checks\n",checks);
}

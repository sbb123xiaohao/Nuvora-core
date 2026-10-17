/* C++ language-core sample linked to musl; no libstdc++/exceptions/RTTI yet. */
#include <stdio.h>
#include <stdlib.h>
template<class T> T sum(const T *values,int count) { T n{};for(int i=0;i<count;i++)n+=values[i];return n; }
class Block { int id_;public: explicit Block(int id):id_(id){} int score(int scale) const {return id_*scale;} };
int main() {
    int *values=static_cast<int *>(malloc(4*sizeof(int)));if(!values)return 1;
    for(int i=0;i<4;i++)values[i]=Block(i+1).score(3);
    printf("C++ classes/templates: sum=%d\n",sum(values,4));free(values);puts("C++ CORE PASS");return 0;
}

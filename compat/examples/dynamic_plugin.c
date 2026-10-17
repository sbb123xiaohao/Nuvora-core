static __thread int value=23;
__attribute__((constructor)) static void initialize(void) {value+=19;}
int nv_dynamic_probe(int delta) {value+=delta;return value;}

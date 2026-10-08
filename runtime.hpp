#pragma once


void runtime_init();
void runtime_fini();


extern "C"
int atexit(void (*function)());
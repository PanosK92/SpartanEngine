#include "utilities.h"

void atg_scs::freeArray(double *&data) {
    delete[] data;
    data = nullptr;
}

void atg_scs::freeArray(int *&data) {
    delete[] data;
    data = nullptr;
}

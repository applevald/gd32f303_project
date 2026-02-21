#include "includes.h"

_dlib_t dlib_t = {0};
char version_s[] =  VERSION;

char *get_my_version()
{
     return version_s;
}

static void set_temperature(float val)
{
    dlib_t.temperature = val;
}
static float get_temperature()
{
    return dlib_t.temperature;
}

static _dlib_op dlib_op =
{
    .set_temperature = set_temperature,
    .get_temperature = get_temperature,
};

_dlib_op *get_dlib_op(void)
{
     return (_dlib_op *)&dlib_op;
}

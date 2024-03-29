#ifndef _SUNAE_H
#define _SUNAE_H

// this is the (more) original sunae.h from Rob Seals
// the one associated with the old model had lots of 
// fields that had nothing to do with sunae

#include <math.h>

typedef struct {
    int           year, doy;
    double        hour, lat, lon,
                /* azimuth, elevation, hour angle, declination,
                   solar distance, zenith, air mass */
                az, el, ha, dec, zen, soldst, am;
} ae_pack;

double sunae(ae_pack *);
double airmass(double el);

#ifndef M_DTOR
#define M_DTOR  0.0174532925199433
#define M_RTOD  57.2957795130823230
#define M_2PI   6.2831853071795862320E0
#define M_HTOR  0.2617993877991494
#define M_RTOH  3.8197186342054881
#define M_HTOD  15.0
#define M_DTOH  0.0666666666666667
#endif

#endif

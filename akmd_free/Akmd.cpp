/*
 * Free, open-source replacement of the closed source akmd driver written
 * by AKM. This program is the user-space counterpart of akm8973 and bma150
 * sensors found on various Android phones.
 *
 * Copyright Antti S. Lankila, 2010, licensed under GPL.
 *
 * The device node to read data from is called akm8973_daemon. The control
 * device node is akm8973_aot. The libsensors from android talks to
 * akm8973_aot. The akmd samples the chip data and performs the analysis.
 * The measuring is inherently a slow process, and therefore a cached
 * copy of results is periodically updated to the /dev/input node "compass"
 * using an ioctl on the akm8973_daemon.
 */

#include <math.h>
#include <sys/time.h>
#include <time.h>

#include "Akmd.hpp"

namespace akmd {

Akmd::Akmd(ChipReader* orientation_reader,
    ChipReader* magnetometer_reader,
    ChipReader* accelerometer_reader,
    ChipReader* temperature_reader,
    DataPublisher* result_writer)
    : magnetometer(120,false)
{
    this->orientation_reader = orientation_reader;
    this->magnetometer_reader = magnetometer_reader;
    this->accelerometer_reader = accelerometer_reader;
    this->temperature_reader = temperature_reader;
    this->result_writer = result_writer;
    delay_min = 0;
    delay_max = 0;
}

Akmd::~Akmd() {
}

void Akmd::fill_result_vector(Vector o, Vector a, Vector m, short temperature, short* out)
{
    
    /* Establish the angle in E */
    
    out[0] =  (int)roundf(360-o.x);
    /* pitch */
    out[2] = -(int)roundf(o.y);    
    /* roll */
    out[1] = -(int)roundf(o.z);

    out[3] = temperature;
    out[4] = 3;
    out[5] = 3;

//dont know why
/*    out[6] =   roundf(a.x);
    out[7] =   roundf(a.z);
    out[8] =  -roundf(a.y);*/

    out[6] =  roundf(a.y);  
    out[7] = -roundf(a.x);
    out[8] =  roundf(a.z);

    out[9] =  -roundf(m.y);
    out[10] = -roundf(m.x);
    out[11] = -roundf(m.z);
}

/****************************************************************************/
/* Raw mechanics of sensor reading                                          */
/****************************************************************************/
void Akmd::sleep_until_next_update()
{
    int delay = 200; /* Based on SensorManager.SENSOR_DELAY_NORMAL */
    int candidate_delay;

    ChipReader* chips[3] = { magnetometer_reader, accelerometer_reader, temperature_reader };
    for (int i = 0; i < 3; i ++) {
        /* Use the value from whoever wants the shortest delay,
         * but -1 means this chip doesn't care */
        int candidate_delay = chips[i]->get_delay();
        if (candidate_delay > 0 && candidate_delay < delay) {
            delay = candidate_delay;
            //LOGI("candidate delay= %d",candidate_delay);
        }
    }

    /* override slow update delay */
    if (delay >= 200 && delay_max > 200) {
        delay = delay_max;
    }
    /* Decide if we want "fast" updates or "slow" updates.
     * FASTEST, GAME and UI are defined as <= 60.
     *
     * The reason I do this stuff is to guarantee some stability in the
     * sampling, and to make sure that accelerometer -- which only gives
     * about 25 good values per second -- has averaged sufficient samples.
     * I sample twice per given interval, but no faster than is practically
     * possible.
     */

    if (delay_min > 0 && delay < delay_min) {
        delay = delay_min;
    }

    if (delay <= 60) {
        delay = 60;
    }
    // else {
        /* divide by 2 to get 2 real samples for the slow modes too. */
        //delay /= 2;
    //}
    //LOGI("delay= %d",delay);
    /* Find out how long to sleep so that we achieve true periodic tick. */ 
    struct timeval current_time;
    SUCCEED(gettimeofday(&current_time, NULL) == 0);
    next_update.tv_usec += delay * 1000;
    next_update.tv_sec += next_update.tv_usec / 1000000;
    next_update.tv_usec %= 1000000;

    int sleep_time = (next_update.tv_sec - current_time.tv_sec) * 1000
                    + (next_update.tv_usec - current_time.tv_usec) / 1000;
    if (sleep_time < 0) {
        /* Have we fallen behind by more than one update? Resync.
         * I'm going to be silent about this, because these things happen
         * every now and then, and it isn't horribly destructive. */
        if (sleep_time < -delay) {
            next_update = current_time;
        }
        return;
    }

    struct timespec interval;
    interval.tv_sec = sleep_time / 1000;
    interval.tv_nsec = 1000000 * (sleep_time % 1000);
    SUCCEED(nanosleep(&interval, NULL) == 0);
}

void Akmd::set_delays() {
    char dmax[6]; 
    char dmin[6]; 
    int d_min = 0;
    int d_max = 0;

    FILE *fmin = fopen("/data/misc/delay_min.txt","r");
    if (fmin > 0) {
        fread(dmin, sizeof(int), 1, fmin);
        fclose(fmin);
        d_min = parse_delay(dmin);
    }
    FILE *fmax = fopen("/data/misc/delay_max.txt","r");
    if (fmax > 0) {
        fread(dmax, sizeof(int), 1, fmax);
        fclose(fmax);
        d_max = parse_delay(dmax);
    }
    if (d_min >= 60) {
        delay_min = d_min;
    }
    if (d_max >= d_min) {
        delay_max = d_max;
    }
}

int Akmd::parse_delay(char *d) {
    int delay;
    sscanf(d, "%d",&delay);
    return delay;
}

void Akmd::measure()
{
    orientation_reader->measure();
    accelerometer_reader->measure();
    magnetometer_reader->measure();
    temperature_reader->measure();

    Vector o = orientation_reader->read();
    Vector a = accelerometer_reader->read();
    Vector m = magnetometer_reader->read();
    Vector temp = temperature_reader->read();
    short temperature = (short) temp.x;

    /* Calculate and set data readable on compass input. */
    short final_data[12];
    fill_result_vector(o, a, m, temperature, final_data);
    result_writer->publish(final_data);
}

void Akmd::start()
{
    accelerometer_reader->start();
    magnetometer_reader->start();
    temperature_reader->start();
    SUCCEED(gettimeofday(&next_update, NULL) == 0);
}

void Akmd::stop()
{
    accelerometer_reader->stop();
    magnetometer_reader->stop();
    temperature_reader->stop();
}

}

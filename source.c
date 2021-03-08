#include <stdio.h>    // for printf
#include <fcntl.h>    // for open
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <gpiod.h>    //libgpiod
#include <time.h>     //precise timing
#include <signal.h>   //timer signaling
#include <pthread.h>  //command thread

//basically inline functions -> makes code much cleaner in a couple places
#define CHECKOVERFLOW(timespec_obj) if(timespec_obj.tv_nsec >= 1000000000){timespec_obj.tv_nsec -= 1000000000;timespec_obj.tv_sec++;}
#define CHECKUNDERFLOW(timespec_obj) if(timespec_obj.tv_nsec < 0){timespec_obj.tv_nsec += 1000000000;timespec_obj.tv_sec--;}

//globals
int dc_r, dc_g, dc_b;   bool exited_menu; int level_shifted;
struct gpiod_chip *chip;
struct gpiod_line *lineG;
struct itimerspec green_led_spec;
timer_t green_led_timer;

int measureDistance(int n_measurements)
{
    //open and initialize new lines for tigger and echo
    struct gpiod_line *trig_line, *echo_line;
    struct gpiod_line_event echo_event;

    trig_line = gpiod_chip_get_line(chip, 22);
    echo_line = gpiod_chip_get_line(chip, 23);

    gpiod_line_request_output(trig_line, "gpio_state", GPIOD_LINE_ACTIVE_STATE_LOW);    //setup for 10us gpio output
    gpiod_line_request_both_edges_events(echo_line, "gpio_state");                      //setup for event rise/fall events later

    //timespecs for measuring pulses, sending 10us pulse, and timeout on receiving pulse
    struct timespec time, time_rise, time_fall, timeout = {0, (long)50000000};       //50 ms timeout for event wait capture later

    //used to sum measurements across n loops
    int total_dist = 0;

    for(int i = 0; i < n_measurements; i++)
    {
        //prep timespec for sleep
        clock_gettime(CLOCK_MONOTONIC, &time);
        time.tv_nsec += 10000;      //10us
        CHECKOVERFLOW(time)

        //send 10us pulse
        gpiod_line_set_value(trig_line, 1);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, NULL);
        gpiod_line_set_value(trig_line, 0);

        //receive and time pulse on echo line
        wait_rise:
        gpiod_line_event_wait(echo_line, &timeout);     //wait for rise
        gpiod_line_event_read(echo_line, &echo_event);
        if(echo_event.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
        {
            clock_gettime(CLOCK_MONOTONIC, &time_rise);     //get time
        }
        else {goto wait_rise;}

        //capture moment of echo pulse end
        wait_fall:
        gpiod_line_event_wait(echo_line, NULL);         //wait for fall
        gpiod_line_event_read(echo_line, &echo_event);
        if(echo_event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
        {
            clock_gettime(CLOCK_MONOTONIC, &time_fall);     //get time
        }
        else {goto wait_fall;}

        //arithmetic to get distance
        time.tv_nsec = time_fall.tv_nsec - time_rise.tv_nsec;
        CHECKUNDERFLOW(time)

        //sum distance
        total_dist += (340.0*(time.tv_nsec))/20000000;
    }

    //release chip & lines
    gpiod_line_release(trig_line);
    gpiod_line_release(echo_line);

    return total_dist/n_measurements;
}

void* commandTask(void* arg)
{
    //to block timer signals
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    //input buffer
    char buffer[32];        
    char *input = buffer;       //to avoid getline parameter warning.. why does that happen?
    size_t buffer_len = 32;     //can't use 32*sizeof(char) ??

    printf("Accepting Commands: \"RGB-intensity x y z\" | \"distance-measure n\" | \"exit\"\n");

    while(!exited_menu)       //if exit command is delivered
    {
        memset(&buffer, 0, buffer_len);         //clears input buffer each loop
        getline(&input, &buffer_len, stdin);    //read line

        if (strstr(input, "RGB-intensity") || strstr(input, "rgb"))
        {
            strtok(input, " ");
            
            //grabs from input buffer
            dc_r = 200000 * atoi(strtok(NULL, " "));        
            dc_g = 200000 * atoi(strtok(NULL, " "));
            dc_b = 200000 * atoi(strtok(NULL, " "));

            //set initial duty cycles to 100% for both channels (this is 20,000,000 ns (20 ms))
            int fd_channel0 = open("/sys/class/pwm/pwmchip0/pwm0/duty_cycle", O_RDWR);
            int fd_channel1 = open("/sys/class/pwm/pwmchip0/pwm1/duty_cycle", O_RDWR);
            sprintf(buffer, "%d", dc_r); 
            write(fd_channel0, buffer, strlen(buffer));  
            sprintf(buffer, "%d", dc_b); 
            write(fd_channel1, buffer, strlen(buffer));
            memset(&buffer, 0, buffer_len);                 //clear buffer to be safe
        }
        else if(strstr(input, "distance-measure"))
        {
            strtok(input, " ");

            //calls measurement function -> blocking, no other commands will be taken during this time
            int n_measurements = atoi(strtok(NULL, " "));
            printf("\nCalculated %d cm average across %d measurements\n\n", measureDistance(n_measurements), n_measurements);
        }
        else if(strstr(input, "exit"))
        {
            //to exit this thread AND main thread
            exited_menu = 1;
        }
        else
        {
            printf("Unrecognized command!\n");
        }
    }

    return NULL;
}

void softwareControlledPWMCallback(int sig, siginfo_t *si, void *uc)
{
    level_shifted = (!level_shifted) & 1;           //logical NOT on first bit of integer
    
    //conditionallys sets level of green led line
    if(dc_g == 20000000)
    {
        gpiod_line_set_value(lineG, 1);
    }
    else if(dc_g == 0)
    {
        gpiod_line_set_value(lineG, 0);
    }
    else
    {
        gpiod_line_set_value(lineG, level_shifted);
    }

    //init timer with next level shift time
    green_led_spec.it_value.tv_nsec = (level_shifted ? dc_g+1 : 20000001 - dc_g);
    timer_settime(green_led_timer, 0, &green_led_spec, NULL);
}


int main(int argc, char** argv)
{
    //init chip and lines for libgpiod green led control
    chip = gpiod_chip_open("/dev/gpiochip0");
    lineG = gpiod_chip_get_line(chip, 25);
    gpiod_line_request_output(lineG, "gpio_state", GPIOD_LINE_ACTIVE_STATE_LOW);
    gpiod_line_set_value(lineG, 1);

    //command and measurement thread
    pthread_t command_and_dmeasure_thread;
	pthread_create(&command_and_dmeasure_thread, NULL, &commandTask, NULL);

    //initial duty cycles - 20ms
    dc_r = 20000000;
    dc_g = 20000000;
    dc_b = 20000000;

    //init buffer for writing to file descriptors
    char buffer[32] = {0};
    
    //export to user space
    int fd = open("/sys/class/pwm/pwmchip0/export", O_RDWR);
    write(fd, buffer, strlen(buffer));                          //export pwm0
	sprintf(buffer, "%d", 1);       
    write(fd, buffer, strlen(buffer));                          //export pwm1
    memset(&buffer, 0, (size_t)32);                 //clear buffer to be safe

    //set period of both pwm channels to 20,000,000 ns (20 ms)
    int fd_channel0 = open("/sys/class/pwm/pwmchip0/pwm0/period", O_RDWR);
    int fd_channel1 = open("/sys/class/pwm/pwmchip0/pwm1/period", O_RDWR);
    sprintf(buffer, "%d", 20000000);   
    write(fd_channel0, buffer, strlen(buffer));  
    write(fd_channel1, buffer, strlen(buffer));

    //set initial duty cycles to 100% for both channels (this is 20,000,000 ns (20 ms))
    fd_channel0 = open("/sys/class/pwm/pwmchip0/pwm0/duty_cycle", O_RDWR);
    fd_channel1 = open("/sys/class/pwm/pwmchip0/pwm1/duty_cycle", O_RDWR);
    write(fd_channel0, buffer, strlen(buffer));  
    write(fd_channel1, buffer, strlen(buffer));
    memset(&buffer, 0, (size_t)32);                 //clear buffer to be safe

    //enable both channels
	fd_channel0 = open("/sys/class/pwm/pwmchip0/pwm0/enable", O_RDWR);
	fd_channel1 = open("/sys/class/pwm/pwmchip0/pwm1/enable", O_RDWR);
	sprintf(buffer, "%d", 1);
	write(fd_channel0, buffer, strlen(buffer));
	write(fd_channel1, buffer, strlen(buffer));
    memset(&buffer, 0, (size_t)32);                 //clear buffer to be safe

    //user signal callback handler -> tells main loop to change pwm
    struct sigaction sig_action;
    sig_action.sa_flags = SA_SIGINFO;
    sig_action.sa_sigaction = softwareControlledPWMCallback;
    sigaction(SIGUSR1, &sig_action, NULL);

    //create event for timer -> sends SIGUSR1 to this thread
    struct sigevent sig_event0;
    sig_event0.sigev_notify = SIGEV_SIGNAL;
    sig_event0.sigev_signo = SIGUSR1;
    sig_event0.sigev_value.sival_ptr = &green_led_timer;

    //initial timer spec values -> will fix itself to match pwm at first event
    green_led_spec.it_value.tv_nsec = 20000000;
    
    //create and start timer
    timer_create(CLOCK_REALTIME, &sig_event0, &green_led_timer);
    timer_settime(green_led_timer, 0, &green_led_spec, NULL);

    while(!exited_menu) {};     //a join seems to block timer event

    //disable both channels
    sprintf(buffer, "%d", 0);
	write(fd_channel0, buffer, strlen(buffer));
	write(fd_channel1, buffer, strlen(buffer));
    memset(&buffer, 0, (size_t)32);                 //clear buffer to be safe

    //unexport back to kernel space
    fd_channel0 = open("/sys/class/pwm/pwmchip0/unexport", O_RDWR);
    sprintf(buffer, "%d", 0); 
    write(fd, buffer, strlen(buffer));                          //unexport pwm0
	sprintf(buffer, "%d", 1);       
    write(fd, buffer, strlen(buffer));                          //unexport pwm1

    gpiod_line_release(lineG);                      //release and close lines and chips
    gpiod_chip_close(chip);
}
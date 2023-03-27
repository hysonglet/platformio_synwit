
#include <SWM341.h>
#include "synwit_board.h"
#include <stdio.h>

int main(void)
{
    uart0_init();
    rtc_init(2023, 3, 25, 19, 45, 0, true, true);
    rtc_alarm_init(RTC_SUN|RTC_MON|RTC_THU|RTC_WED|RTC_TUE|RTC_FRI|RTC_SAT,
            19, 45, 30, true);
    for (;;){
    }
    return 0;
}

void RTC_Handler(void)
{
    if(RTC_IntAlarmStat(RTC)){
		RTC_IntAlarmClr(RTC);
        printf("alarm comming...\n");
    }
	if(RTC_IntSecondStat(RTC)){
		RTC_IntSecondClr(RTC);
        RTC_DateTime dt;
        rtc_datetime_get(&dt);
        printf("second int %u/%u/%u %u:%u:%u\n", dt.Year, dt.Month, dt.Date,
                                        dt.Hour, dt.Minute, dt.Second);
	}
    if(RTC_IntMinuteStat(RTC)){
        RTC_IntMinuteClr(RTC);
        RTC_DateTime dt;
        rtc_datetime_get(&dt);
        printf("minute int %u/%u/%u %u:%u:%u\n", dt.Year, dt.Month, dt.Day,
                                        dt.Hour, dt.Minute, dt.Second);
    }
}


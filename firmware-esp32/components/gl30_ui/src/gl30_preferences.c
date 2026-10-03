#include "gl30_preferences.h"

#include "v6_protocol.h"

#include <string.h>

static bool valid_step(int value) {
    return value==1 || value==5 || value==10;
}

static bool valid_preferences(const gl30_preferences *p) {
    return p!=NULL && p->volume<=100u && p->light_effect<=3u &&
        p->light_color<=5u && p->light_brightness<=100u &&
        p->screen_brightness>=10u && p->screen_brightness<=100u &&
        p->language<=1u && valid_step(p->timer_step_minutes) &&
        valid_step(p->volume_step_percent) && p->stopwatch_show_centis<=1u &&
        p->alarm_enabled<=1u && p->weather_fahrenheit<=1u &&
        p->feel_profile<=2u && p->calendar_monday_first<=1u;
}

static void store_u16_le(uint8_t *out,uint16_t value) {
    out[0]=(uint8_t)value;
    out[1]=(uint8_t)(value>>8u);
}

static uint16_t load_u16_le(const uint8_t *in) {
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1]<<8u));
}

static void store_u32_le(uint8_t *out,uint32_t value) {
    out[0]=(uint8_t)value;
    out[1]=(uint8_t)(value>>8u);
    out[2]=(uint8_t)(value>>16u);
    out[3]=(uint8_t)(value>>24u);
}

static uint32_t load_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1]<<8u) |
        ((uint32_t)in[2]<<16u) | ((uint32_t)in[3]<<24u);
}

bool gl30_preferences_capture(const gl30_model *model,gl30_preferences *out) {
    if(model==NULL || out==NULL || model->volume<0 || model->volume>100 ||
       model->light_effect<0 || model->light_effect>3 ||
       model->light_color<0 || model->light_color>5 ||
       model->light_brightness<0 || model->light_brightness>100 ||
       model->screen_brightness<10 || model->screen_brightness>100 ||
       (model->language!=GL30_LANGUAGE_ENGLISH &&
        model->language!=GL30_LANGUAGE_CHINESE) ||
       !valid_step(model->timer_step_minutes) ||
       !valid_step(model->volume_step_percent) ||
       model->feel_profile<0 || model->feel_profile>2) {
        return false;
    }

    const gl30_preferences candidate={
        .volume=(uint8_t)model->volume,
        .light_effect=(uint8_t)model->light_effect,
        .light_color=(uint8_t)model->light_color,
        .light_brightness=(uint8_t)model->light_brightness,
        .screen_brightness=(uint8_t)model->screen_brightness,
        .language=(uint8_t)model->language,
        .timer_step_minutes=(uint8_t)model->timer_step_minutes,
        .volume_step_percent=(uint8_t)model->volume_step_percent,
        .stopwatch_show_centis=model->stopwatch_show_centis?1u:0u,
        .alarm_enabled=model->alarm_enabled?1u:0u,
        .weather_fahrenheit=model->weather_fahrenheit?1u:0u,
        .feel_profile=(uint8_t)model->feel_profile,
        .calendar_monday_first=model->calendar_monday_first?1u:0u
    };
    *out=candidate;
    return true;
}

bool gl30_preferences_apply(gl30_model *model,const gl30_preferences *preferences) {
    if(model==NULL || !valid_preferences(preferences)) return false;

    model->volume=(int)preferences->volume;
    model->light_effect=(int)preferences->light_effect;
    model->light_color=(int)preferences->light_color;
    model->light_brightness=(int)preferences->light_brightness;
    model->screen_brightness=(int)preferences->screen_brightness;
    model->language=(gl30_language)preferences->language;
    model->timer_step_minutes=(int)preferences->timer_step_minutes;
    model->volume_step_percent=(int)preferences->volume_step_percent;
    model->stopwatch_show_centis=preferences->stopwatch_show_centis!=0u;
    model->alarm_enabled=preferences->alarm_enabled!=0u;
    model->weather_fahrenheit=preferences->weather_fahrenheit!=0u;
    model->feel_profile=(int)preferences->feel_profile;
    model->calendar_monday_first=preferences->calendar_monday_first!=0u;
    return true;
}

bool gl30_preferences_equal(const gl30_preferences *a,const gl30_preferences *b) {
    return a!=NULL && b!=NULL &&
        a->volume==b->volume && a->light_effect==b->light_effect &&
        a->light_color==b->light_color && a->light_brightness==b->light_brightness &&
        a->screen_brightness==b->screen_brightness && a->language==b->language &&
        a->timer_step_minutes==b->timer_step_minutes &&
        a->volume_step_percent==b->volume_step_percent &&
        a->stopwatch_show_centis==b->stopwatch_show_centis &&
        a->alarm_enabled==b->alarm_enabled &&
        a->weather_fahrenheit==b->weather_fahrenheit &&
        a->feel_profile==b->feel_profile &&
        a->calendar_monday_first==b->calendar_monday_first;
}

bool gl30_preferences_encode(const gl30_preferences *preferences,
                             uint8_t out[GL30_PREFERENCES_RECORD_BYTES]) {
    if(out==NULL || !valid_preferences(preferences)) return false;

    uint8_t record[GL30_PREFERENCES_RECORD_BYTES]={0};
    record[0]='G'; record[1]='L'; record[2]='3'; record[3]='0';
    store_u16_le(&record[4],1u);
    store_u16_le(&record[6],(uint16_t)GL30_PREFERENCES_RECORD_BYTES);
    record[8]=preferences->volume;
    record[9]=preferences->light_effect;
    record[10]=preferences->light_color;
    record[11]=preferences->light_brightness;
    record[12]=preferences->screen_brightness;
    record[13]=preferences->language;
    record[14]=preferences->timer_step_minutes;
    record[15]=preferences->volume_step_percent;
    record[16]=preferences->stopwatch_show_centis;
    record[17]=preferences->alarm_enabled;
    record[18]=preferences->weather_fahrenheit;
    record[19]=preferences->feel_profile;
    record[20]=preferences->calendar_monday_first;

    uint32_t crc;
    gl30_crc32c(record,24u,&crc);
    store_u32_le(&record[24],crc);
    memcpy(out,record,sizeof(record));
    return true;
}

bool gl30_preferences_decode(const uint8_t *data,size_t length,
                             gl30_preferences *out) {
    if(data==NULL || out==NULL || length!=GL30_PREFERENCES_RECORD_BYTES ||
       data[0]!='G' || data[1]!='L' || data[2]!='3' || data[3]!='0' ||
       load_u16_le(&data[4])!=1u ||
       load_u16_le(&data[6])!=GL30_PREFERENCES_RECORD_BYTES ||
       data[21]!=0u || data[22]!=0u || data[23]!=0u) {
        return false;
    }

    uint32_t crc;
    gl30_crc32c(data,24u,&crc);
    if(load_u32_le(&data[24])!=crc) return false;

    const gl30_preferences candidate={
        .volume=data[8],
        .light_effect=data[9],
        .light_color=data[10],
        .light_brightness=data[11],
        .screen_brightness=data[12],
        .language=data[13],
        .timer_step_minutes=data[14],
        .volume_step_percent=data[15],
        .stopwatch_show_centis=data[16],
        .alarm_enabled=data[17],
        .weather_fahrenheit=data[18],
        .feel_profile=data[19],
        .calendar_monday_first=data[20]
    };
    if(!valid_preferences(&candidate)) return false;

    *out=candidate;
    return true;
}

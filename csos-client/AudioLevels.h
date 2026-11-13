#pragma once
#include <stdint.h>
#include <math.h>
inline int audioDb(int db){return db<0?0:db>20?20:db;}
inline int32_t audioGainQ16(int db){return lroundf(65536*powf(10,audioDb(db)/20.0f));}
inline int16_t audioLimit(int64_t x,uint32_t& clipped){if(x>32000){clipped++;return 32000;}if(x< -32000){clipped++;return -32000;}return int16_t(x);}
inline int16_t audioOutput(int sample,int volume,int32_t gain,uint32_t& clipped){return audioLimit(int64_t(sample)*volume*gain/(10*65536),clipped);}
// 0 dB is unity. No hidden normalization or gain on microphone input.
inline int16_t audioMicrophone(int16_t sample,int32_t gain,uint32_t& clipped){return audioLimit(int64_t(sample)*gain/65536,clipped);}

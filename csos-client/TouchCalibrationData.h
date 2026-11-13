#pragma once
#include "calibration.hpp"
#include <algorithm>
#include <cstdint>
constexpr Point TOUCH_CAL_TARGETS[9]={{16,16},{160,16},{303,16},{16,120},{160,120},{303,120},{16,223},{160,223},{303,223}};
constexpr Point TOUCH_CHECK_TARGETS[8]={{8,8},{160,8},{311,8},{311,120},{311,231},{160,231},{8,231},{8,120}};
struct TouchCapture {
    static constexpr int CAPACITY=96;
    int xs[CAPACITY]={},ys[CAPACITY]={},count=0,next=0,warmup=0,low=0;
    bool accepted=false;
    Point raw={};
    int minZ=8192,maxZ=0,below=0,samples=0,spread=0;
    void reset(){*this=TouchCapture{};}
    // Group samples for calibration only; does not alter operational DirectTouch.
    bool sample(int x,int y,int driverZ,int rawZ,const Calibration* scale=nullptr,int threshold=400,bool fixedWindow=false){
        if(rawZ>75){minZ=std::min(minZ,rawZ);maxZ=std::max(maxZ,rawZ);samples++;if(driverZ<400)below++;}
        if(driverZ<threshold){low=rawZ<=75?low+1:0;return false;}
        low=0;if(warmup++<3||accepted)return false;
        xs[next]=x;ys[next]=y;next=(next+1)%CAPACITY;count=std::min(CAPACITY,count+1);
        if(count<20)return false;
        int xx[CAPACITY],yy[CAPACITY];
        std::copy(xs,xs+count,xx);std::copy(ys,ys+count,yy);
        std::sort(xx,xx+count);std::sort(yy,yy+count);
        const int trim=count/10;
        spread=std::max(xx[count-1-trim]-xx[trim],yy[count-1-trim]-yy[trim]);
        if(fixedWindow){
            // Collect a robust representative point; judge accuracy on separate
            // targets, not by demanding the old map's precision before fitting.
            if(count<CAPACITY)return false;
        }else if(scale&&scale->valid){
            if(count<32)return false;
            // Estimated precision of the median, not the full noisy sample range.
            // This is a collection gate; independent targets verify actual accuracy.
            const double k=1.82/std::sqrt(double(count));
            const double ux=k*(xx[3*count/4]-xx[count/4]);
            const double uy=k*(yy[3*count/4]-yy[count/4]);
            if(std::fabs(scale->a)*ux+std::fabs(scale->b)*uy>2.0||
               std::fabs(scale->d)*ux+std::fabs(scale->e)*uy>2.0)return false;
            // Reject drift between chronological halves, even if dispersion is low.
            int hx[CAPACITY],hy[CAPACITY];
            for(int i=0;i<count;i++){const int j=(next-count+i+CAPACITY)%CAPACITY;hx[i]=xs[j];hy[i]=ys[j];}
            const int half=count/2;
            std::sort(hx,hx+half);std::sort(hx+half,hx+count);
            std::sort(hy,hy+half);std::sort(hy+half,hy+count);
            Point first=scale->map({double(hx[half/2]),double(hy[half/2])});
            Point last=scale->map({double(hx[half+(count-half)/2]),double(hy[half+(count-half)/2])});
            if(std::hypot(first.x-last.x,first.y-last.y)>3.0)return false;
        }else if(spread>60)return false;
        raw={double(xx[count/2]),double(yy[count/2])};accepted=true;return true;
    }
    bool released()const{return low>=3;}
};
struct TouchErrors {
    int count=0,missed=0;double squared=0,maximum=0;
    void add(double error){count++;squared+=error*error;maximum=std::max(maximum,error);}
    double rms()const{return count?std::sqrt(squared/count):0;}
};

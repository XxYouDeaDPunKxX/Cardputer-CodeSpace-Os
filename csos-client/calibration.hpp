#pragma once
#include <cmath>
struct Point { double x, y; };
struct Calibration {
    double a=0,b=0,c=0,d=0,e=0,f=0;
    bool valid=false;
    bool fit(const Point* raw, const Point* screen) {
        valid=false;
        const double x1=raw[1].x-raw[0].x, y1=raw[1].y-raw[0].y;
        const double x2=raw[2].x-raw[0].x, y2=raw[2].y-raw[0].y;
        const double det=x1*y2-x2*y1;
        if (std::fabs(det)<1000) return false;
        a=((screen[1].x-screen[0].x)*y2-(screen[2].x-screen[0].x)*y1)/det;
        b=(x1*(screen[2].x-screen[0].x)-x2*(screen[1].x-screen[0].x))/det;
        d=((screen[1].y-screen[0].y)*y2-(screen[2].y-screen[0].y)*y1)/det;
        e=(x1*(screen[2].y-screen[0].y)-x2*(screen[1].y-screen[0].y))/det;
        c=screen[0].x-a*raw[0].x-b*raw[0].y;
        f=screen[0].y-d*raw[0].x-e*raw[0].y;
        valid=true;
        return true;
    }
    Point map(Point raw) const { return {a*raw.x+b*raw.y+c,d*raw.x+e*raw.y+f}; }
    bool fitMany(const Point* raw,const Point* screen,int count) {
        valid=false;
        if(count<3)return false;
        double matrix[3][5]={};
        for(int i=0;i<count;i++){
            const double v[]={1.0,(raw[i].x-2048)/2048,(raw[i].y-2048)/2048};
            for(int r=0;r<3;r++){
                for(int col=0;col<3;col++)matrix[r][col]+=v[r]*v[col];
                matrix[r][3]+=v[r]*screen[i].x;matrix[r][4]+=v[r]*screen[i].y;
            }
        }
        for(int col=0;col<3;col++){
            int pivot=col;
            for(int r=col+1;r<3;r++)if(std::fabs(matrix[r][col])>std::fabs(matrix[pivot][col]))pivot=r;
            if(std::fabs(matrix[pivot][col])<1e-8)return false;
            for(int j=0;j<5;j++){double t=matrix[pivot][j];matrix[pivot][j]=matrix[col][j];matrix[col][j]=t;}
            const double divisor=matrix[col][col];
            for(int j=0;j<5;j++)matrix[col][j]/=divisor;
            for(int r=0;r<3;r++)if(r!=col){
                const double factor=matrix[r][col];
                for(int j=0;j<5;j++)matrix[r][j]-=factor*matrix[col][j];
            }
        }
        a=matrix[1][3]/2048;b=matrix[2][3]/2048;c=matrix[0][3]-2048*(a+b);
        d=matrix[1][4]/2048;e=matrix[2][4]/2048;f=matrix[0][4]-2048*(d+e);
        valid=std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)&&std::isfinite(d)&&std::isfinite(e)&&std::isfinite(f);
        return valid;
    }
};

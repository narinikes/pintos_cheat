#ifndef FIXED_POINT_H
#define FIXED_POINT_H

typedef int fixed_point;
static const int f = 1 << 14;

// fixed_point integer_to_fixed_point(int n);
// fixed_point fixed_point_to_int_toZero(fixed_point x);
// fixed_point fixed_point_to_int_toNearest(fixed_point x);
// fixed_point fixed_point_sum_fip(fixed_point x, fixed_point y);
// fixed_point fixed_point_sum_int(fixed_point x, int n);
// fixed_point fixed_point_sub_fip(fixed_point x, fixed_point y);
// fixed_point fixed_point_sub_int(fixed_point x, int n);
// fixed_point fixed_point_mul_fip(fixed_point x, fixed_point y);
// fixed_point fixed_point_mul_int(fixed_point x, int n);
// fixed_point fixed_point_div_fip(fixed_point x, fixed_point y);
// fixed_point fixed_point_div_int(fixed_point x, int n);

static inline fixed_point integer_to_fixed_point(int n){
    return n * f;
}

static inline fixed_point fixed_point_to_int_toZero(fixed_point x){
    return x / f;
}

static inline fixed_point fixed_point_to_int_toNearest(fixed_point x){
    if (x >= 0)
        return (x + f / 2) / f;
    else
        return (x - f / 2) / f;
}

static inline fixed_point fixed_point_sum_fip(fixed_point x, fixed_point y){
    return x + y;
}

static inline fixed_point fixed_point_sum_int(fixed_point x, int n){
    return x + n * f;
}

static inline fixed_point fixed_point_sub_fip(fixed_point x, fixed_point y){
    return x - y;
}

static inline fixed_point fixed_point_sub_int(fixed_point x, int n){
    return x - n * f;
}

static inline fixed_point fixed_point_mul_fip(fixed_point x, fixed_point y){
    return ((int64_t) x) * y / f;
}

static inline fixed_point fixed_point_mul_int(fixed_point x, int n){
    return x * n;
}

static inline fixed_point fixed_point_div_fip(fixed_point x, fixed_point y){
    return ((int64_t) x) * f / y;
}

static inline fixed_point fixed_point_div_int(fixed_point x, int n){
    return x / n;
}


#endif
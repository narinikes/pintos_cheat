// #include "threads/fixed_point.h"
// #include <stdint.h>

// const int f = 1 << 14;

// fixed_point integer_to_fixed_point(int n){
//     return n * f;
// }

// fixed_point fixed_point_to_int_toZero(fixed_point x){
//     return x / f;
// }

// fixed_point fixed_point_to_int_toNearest(fixed_point x){
//     if (x >= 0)
//         return (x + f / 2) / f;
//     else
//         return (x - f / 2) / f;
// }

// fixed_point fixed_point_sum_fip(fixed_point x, fixed_point y){
//     return x + y;
// }

// fixed_point fixed_point_sum_int(fixed_point x, int n){
//     return x + n * f;
// }

// fixed_point fixed_point_sub_fip(fixed_point x, fixed_point y){
//     return x - y;
// }

// fixed_point fixed_point_sub_int(fixed_point x, int n){
//     return x - n * f;
// }

// fixed_point fixed_point_mul_fip(fixed_point x, fixed_point y){
//     return ((int64_t) x) * y / f;
// }

// fixed_point fixed_point_mul_int(fixed_point x, int n){
//     return x * n;
// }

// fixed_point fixed_point_div_fip(fixed_point x, fixed_point y){
//     return ((int64_t) x) * f / y;
// }

// fixed_point fixed_point_div_int(fixed_point x, int n){
//     return x / n;
// }

function [gen,exc,gov,freq,stepsize,stoptime] = case9dyn

% case9dyn
% MatDyn dynamic data file
%
% MatDyn
% Copyright (C) 2009 Stijn Cole
% Katholieke Universiteit Leuven
% Dept. Electrical Engineering (ESAT), Div. ELECTA
% Kasteelpark Arenberg 10
% 3001 Leuven-Heverlee, Belgium

%% General data

freq = 50;
stepsize = 0.01;
stoptime = 1;

%% Generator data

% [genmodel excmodel govmodel H D xd xq xd_tr xq_tr Td_tr Tq_tr]
% [ genmodel    excmodel    govmodel    H       D       x       x'     ]

gen=[2  2   1  4.20  0.0125     1.0    0.69    0.31   0.008   10.2   1.5;
     2  2   1  3.03   0.035    2.95    2.82   0.697   0.170   6.56   1.5;
     2  2   1  3.58  0.0304   2.495    2.37   0.531  0.0876    5.7   1.5;
     2  2   1  2.86  0.0295    2.62    2.58   0.436   0.166   5.69   1.5;
     2  2   1  2.60   0.054     6.7     6.2    1.32   0.166    5.4  0.44;
     2  2   1  3.48  0.0224    2.54    2.41     0.5  0.0814    7.3   0.4;
     2  2   1  2.64  0.0332    2.95    2.92    0.49   0.186   5.66   1.5;
     2  2   1  2.43   0.028     2.9    2.80    0.57  0.0911    6.7  0.41;
     2  2   1  3.45  0.0298   2.106    2.05    0.57  0.0587   4.79  1.96;
     2  1   1  50.0   0.003     0.2    0.19    0.06   0.008    7.0   0.7];

%% Exciter data

% [gen Ka  Ta  Ke  Te  Kf  Tf  Aex  Bex  Ur_min  Ur_max]
exc=[1 5.0  0.06    0       0.25    0.04    1.0    0   1.7   -5.0   5.0;
     2 6.2  0.05    0       0.41    0.06    0.5    0   3   -5.0   5.0;
     3 5.0  0.06    0       0.5     0.08    1.0    0   3   -5.0   5.0;
     4 5.0  0.06    0       0.5     0.08    1.0    0   3   -5.0   5.0;
     5 40   0.02    0       0.785   0.03    1.0    0   3   -10.0   10.0;
     6 5.0  0.02    0       0.471   0.08   1.25    0   3   -5.0   5.0;
     7 40   0.02    0       0.73    0.03    1.0    0   3   -6.5   6.5;
     8 5.0  0.02    0       0.528   0.09   1.26    0   3   -5.0   5.0;
     9 40   0.02    0       1.4     0.03    1.0    0   3   -10.5   10.5
     10 0    0       0       0       0       0   0     0       0       0
 ];



%% Governor data
% [gen K  T1  T2  T3  Pup  Pdown  Pmax  Pmin]
gov=[1  0   0   0   0   0   0   0   0;
     2  0   0   0   0   0   0   0   0;
     3  0   0   0   0   0   0   0   0;
     4  0   0   0   0   0   0   0   0;
     5  0   0   0   0   0   0   0   0;
     6  0   0   0   0   0   0   0   0;
     7  0   0   0   0   0   0   0   0;
     8  0   0   0   0   0   0   0   0;
     9  0   0   0   0   0   0   0   0;
     10  0   0   0   0   0   0   0   0];


return;

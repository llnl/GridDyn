function [gen,exc,gov,freq,stepsize,stoptime] = casestaggdyn

% casestaggdyn
% MatDyn dynamic data file
%
% MatDyn
% Copyright (C) 2009 Stijn Cole
% Katholieke Universiteit Leuven
% Dept. Electrical Engineering (ESAT), Div. ELECTA
% Kasteelpark Arenberg 10
% 3001 Leuven-Heverlee, Belgium

%% General data

freq=60;
stepsize = 0.02;
stoptime=.9;

%% Generator data

% [genmodel excmodel govmodel H D xd xq xd_tr xq_tr Td_tr Tq_tr]
gen=[1  1   1   50   0   0.25    0.25    0    0    0 0;
     1  1   1    1   0   1.50    1.50    0    0    0 0];

%% Exciter data

% [gen Ka  Ta  Ke  Te  Kf  Tf  Aex  Bex  Ur_min  Ur_max]
exc=[1  0 0 0 0 0 0 0 0 0 0;
     2  0 0 0 0 0 0 0 0 0 0];

%% Governor data

% [gen K  T1  T2  T3  Pup  Pdown  Pmax  Pmin]
gov=[1 0 0 0 0 0 0 0 0;
     2 0 0 0 0 0 0 0 0];

return;

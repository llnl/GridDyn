function [event,buschange,linechange] = fault

% fault
% MatDyn event data file
%
% MatDyn
% Copyright (C) 2009 Stijn Cole
% Katholieke Universiteit Leuven
% Dept. Electrical Engineering (ESAT), Div. ELECTA
% Kasteelpark Arenberg 10
% 3001 Leuven-Heverlee, Belgium

%%
% event = [time type]
event=[0.2      1;
       0.4    1];

% buschange = [time bus(row)  attribute(col) new_value]
buschange   = [0.2    2   6   -1e10;
               0.31  2   6   0];

% linechange = [time  line(row)  attribute(col) new_value]
linechange = [];

return;

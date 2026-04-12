%% -*- Mode:matlab; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*- */
%
% LLNS Copyright Start
% Copyright (c) 2016, Lawrence Livermore National Security
% This work was performed under the auspices of the U.S. Department
% of Energy by Lawrence Livermore National Laboratory in part under
% Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
% Produced at the Lawrence Livermore National Laboratory.
% All rights reserved.
% For details, see the LICENSE file.
% LLNS Copyright End

%script to plot some data from the two_bus_dynamic_example

ts=timeSeries2('twobusdynout.csv');% replace the file name with the appropriate location
 figure(1)
 hold off;
 plot(ts.time,-ts(5),'LineWidth',3);
 title('Bus 1 Real Generation');
 ylabel('Generation (pu)');
 xlabel('time(s)');

 figure(2);
 hold off;
 freq1=diff(ts(3))./diff(ts.time)/(2.0*pi);
 freq2=diff(ts(4))./diff(ts.time)/(2.0*pi);
 plot(ts.time(2:end),freq1,ts.time(2:end),freq2,'LineWidth',2);
 legend('Bus1','Bus2','Location','SouthEast');
 title('Bus Frequency');
 xlabel('time(s)');
 ylabel('frequency deviation (Hz)');

 figure(3);
 hold off;

 plot(ts.time,ts(1:2),'LineWidth',2);
 legend('Bus1','Bus2','Location','SouthEast');
 title('Bus Voltage');
 xlabel('time(s)');
 ylabel('Voltage(pu)');

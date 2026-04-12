%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\link_tests\link_fault2.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\relay_tests\relay_test_multi.xml';
modelfile='C:\Users\top1\Documents\codeProjects\griddyn_test_cases\180busDynamic_loadDisable.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\gridlabD_tests\Simple_3Bus_mod.xml';
%executable='C:\Users\top1\Documents\codeProjects\transmission\build\gridDynMain\Debug\griddynMain.exe';
executable='C:\Users\top1\Documents\codeProjects\GridDyn\build\src\gridDynMain\Debug\gridDynMain.exe';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\IEEE_test_cases\ieee300.cdf';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\rootFinding_tests\test_gov_limit3.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\load_tests\motorload_test3.xml';
exestring=[executable ' ' modelfile];

%exestring=[exestring ' --flags=ignore_bus_limits --powerflow-output=gridLabDout.csv --powerflow-only'];
%exestring=[executable ' --detect_memory_leak=0 --run_test=link_tests/link_test1_dynamic '];

%exestring=[exestring '  --powerflow-output=ieee300pf_result.csv'];
%exestring=[exestring '  '];
tic
[status,result] = system(exestring);
toc
%Tm2=timeSeries2('motorChange.dat');
Tm2=timeSeries2('dynfault-load-drop.dat');
% figure(1);
% hold off;
%  plts=plot(Tm2.time,Tm2.data(:,1:9),'LineWidth',3);
% set(plts(2),'Color','k'); title('Bus Voltages','FontSize',12);
% legend('Bus 1', 'Bus 2', 'Bus 3', 'Bus 4','Location','Northwest');
%  xlabel('Time(s)','FontSize',12);
%  figure(2);
%  hold off;
% plts=plot(Tm2.time,Tm2.data(:,10:18),'LineWidth',3);
% figure(3);
%  hold off;
% plts=plot(Tm2.time,Tm2.data(:,37:45),'LineWidth',3);
% set(plts(2),'Color','k');
% title('Bus Loads Reactive','FontSize',12);
% ylabel('Reactive Load (MVAR)','FontSize',12);
% xlabel('Time(s)','FontSize',12);
% legend('Bus 1', 'Bus 2', 'Bus 3', 'Bus 4','Location','Northeast');
% figure(3);
% hold off;
% plot(Tm2.time,Tm2.data(:,25),'LineWidth',3)
% title('Exciter field Voltage','FontSize',12);
% ylabel('Field Voltage (pu)','FontSize',12);
% xlabel('Time(s)','FontSize',12);
%
% figure(4);
% hold off
% plot(Tm2.time,Tm2.data(:,2),'b','LineWidth',3);
% hold on;
% plot(Tm3.time,Tm3.data(:,2),'c','LineWidth',3);
% plot(Tm2.time,Tm2.data(:,3),'r','LineWidth',3);
% plot(Tm3.time,Tm3.data(:,3),'m','LineWidth',3);
% hold on;
% title('Bus Voltages Comparison','FontSize',12);
% legend('Bus 2', 'Bus 2 limit', 'Bus 3', 'Bus 3 limit','Location','Northwest');
% xlabel('Time(s)','FontSize',12);
% ylabel('Voltage (pu)');
%
% ltime=[5.15369,19.9104,35.2819,49.9056];
% hold on;
% for tt=ltime
%     plot([tt,tt],[0.98,1.005],'g-.','LineWidth',2);
% end

% figure(1);
% td=Tm2.time(4)-Tm2.time(3);
% plts=plot(Tm2.time(2:end),diff(Tm2.data(:,5:8))./td/2/pi+60,'LineWidth',3);
% set(plts(2),'Color','k');
% title('Bus Frequency','FontSize',12);
% ylabel('Frequency (Hz)','FontSize',12);
% xlabel('Time(s)','FontSize',12);
%
% figure(2);
% plts=plot(Tm2.time,Tm2.data(:,13:16)*100,'LineWidth',3);
% set(plts(2),'Color','k');
% title('Bus Load','FontSize',12);
% ylabel('Load (MW)','FontSize',12);
% xlabel('Time(s)','FontSize',12);
% legend('Bus 1', 'Bus 2', 'Bus 3', 'Bus 4','Location','Northwest');
% figure(4);
% hold off;
% plts=plot(Tm2.time,Tm2.data(:,17:18)*60,'LineWidth',3);
% set(plts(2),'Color','k');
% title('Generator Frequency','FontSize',12);
% ylabel('Frequency (Hz)','FontSize',12);
% legend('Gen A', 'Gen B');
%
% xlabel('Time(s)','FontSize',12);
% figure(5);
% plts=plot(Tm2.time,-Tm2.data(:,9:10)*100,'LineWidth',3);
% set(plts(2),'Color','k');
% title('Generator Output','FontSize',12);
% ylabel('Output power (MW)','FontSize',12);
% legend('Gen A', 'Gen B','Location','Northwest');
% xlabel('Time(s)','FontSize',12);

% figure(6);
% hold off;
% plot(Tm1.time,Tm1.data(:,18)*60,'LineWidth',3);
% hold on;
% plot(Tm2.time,Tm2.data(:,18)*60,'r--','LineWidth',3);
% plot(Tm3.time,Tm3.data(:,18)*60,'k-.','LineWidth',3);
% legend('Scenario 1', 'Scenario 2','Scenario 3','Location','Northeast');
% title('Generator B Frequency','FontSize',12);
% ylabel('Frequency (Hz)','FontSize',12);
% xlabel('Time(s)','FontSize',12);

% figure(1);
% hold off;
% plot(Tm2.time,Tm2.data(:,10),'LineWidth',3);
% title('Motor Slip');
% xlabel('time(s)');
% ylabel('slip (pu)');
%
% figure(2);
% hold off;
% plot(Tm2.time,Tm2.data(:,8:9),'LineWidth',3);
% title('Load Power');
% xlabel('time(s)');
% ylabel('Load (pu)');
% legend('Real','Reactive');
%
% figure(3);
% hold off;
% plot(Tm2.time,Tm2.data(:,1:2),'LineWidth',2);
% title('Bus Voltages');
% xlabel('time(s)');
% ylabel('Voltage (pu)');
% legend('Bus 1','Bus 2','Location','SouthEast');
% axis([0,30,0.98,1.01]);

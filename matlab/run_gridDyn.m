%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\link_tests\link_fault2.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\relay_tests\relay_test_multi.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\input_tests\test_mat_dyn.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\gridlabD_tests\Simple_3Bus_mod.xml';
%executable='C:\Users\top1\Documents\codeProjects\transmission\build\gridDynMain\Debug\griddynMain.exe';
executable= 'C:\Users\top1\Documents\codeProjects\transmission\build\src\gridDynMain\Release\griddynMain.exe';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\IEEE_test_cases\ieee300.cdf';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\rootFinding_tests\test_gov_limit3.xml';
%modelfile='C:\Users\top1\Documents\codeProjects\transmission\test\test_files\load_tests\motorload_test3_stall.xml';
modelfile='C:\Users\top1\Documents\codeProjects\transmission\test\test_files\other_test_cases\180busDynamic_fault.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\examples\180busdyn_test.xml';
%modelfile='E:\My_Documents\Code_projects\transmission_git\test\test_files\load_tests\motorload_test4_stall.xml';
exestring=[executable ' ' modelfile];

%exestring=[exestring ' --flags=ignore_bus_limits --powerflow-output=gridLabDout.csv --powerflow-only'];
%exestring=[exestring '  --powerflow-output=ieee300pf_result.csv'];
exestring=[exestring '  '];
tic
[status,result] = system(exestring);
toc


Tm1=timeSeries2('dynfault-vis.dat');

figure(1);
plot(Tm1.time,Tm1.data(:,1:179));
title('Voltages');

figure(2);
plot(Tm1.time,sum(Tm1(:,716-179:716),2))
title('Total load')

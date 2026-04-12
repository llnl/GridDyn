modelfile='E:\My_Documents\MATLAB\matpower4.0\case14.m';

executable='E:\My_Documents\MATLAB\Power_model\griddynMain.exe';

outputfile='E:\My_Documents\MATLAB\pflow_output.csv';

exestring=[executable ' ' modelfile ' --powerflow-output=' outputfile];

exestring=[exestring ' --flags=ignore_bus_limits'];

[status,result] = system(exestring);

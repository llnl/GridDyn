tfile='E:\My_Documents\Code_projects\transmission_git\build\gridDynMain\dynfault.dat';
Tm2=timeSeries2(tfile);



ofdir='E:\My_Documents\Code_projects\documents\CosimulationLDRD\2015\IEEE_MSCPES\data\';
ofile1=fullfile(ofdir,'dynfault-lat1-100mbps-nobg.csv');
ofile2=fullfile(ofdir,'dynfault-lat100-100mbps-nobg.csv');
ofile3=fullfile(ofdir,'dynfault-timeout10.csv');

Tmo1=timeSeries2(ofile1);
Tmo2=timeSeries2(ofile2);
Tmo3=timeSeries2(ofile3);

%%
figure(1);
fld=42;
plot(Tm2.time,Tm2.data(:,fld),Tmo1.time,Tmo1.data(:,fld),Tmo2.time,Tmo2.data(:,fld),Tmo3.time,Tmo1.data(:,fld))
%%
figure(2);
plot(Tm2.time,Tm2.data(:,1:39));

figure(3);
plot(Tmo1.time,Tmo1.data(:,1:39));
figure(4);
plot(Tmo2.time,Tmo2.data(:,1:39));
figure(5);
plot(Tmo3.time,Tmo3.data(:,1:39));

file='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\loadModels\rampLoad.cpp';

fid=fopen(file,'r');
c=fread(fid,'char=>char');
fclose(fid);
c=c';

ret=getDoubleSet('rampLoad',c);

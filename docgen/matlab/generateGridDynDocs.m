
tfile='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\loadModels\otherLoads.h';

fid=fopen(tfile,'r');
c=fread(fid,'char=>char');
fclose(fid);
c=c';

%a=regexp(c,'class ([^:]*):\s*class\s*([^\s]*)','tokenExtents');

a=regexp(c,'\nclass ([^:*\/;{]*):\s*public ([^\s]*)','tokenExtents');
cls=struct('name','','parent','','variables',[]);
for pp=1:length(a)
    cls(pp).name=strtrim(c(a{pp}(1,1):a{pp}(1,2)));
    cls(pp).parent=strtrim(c(a{pp}(2,1):a{pp}(2,2)));
    cblock=extractCodeBlock(c,a{pp}(2,2)+1);
    vblk=getClassVariables(cblock);
    cls(pp).variables=vblk;
end

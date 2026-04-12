dv1='C:\Users\top1\Documents\codeProjects\transmission\build\test\solverCapture.bin';
dv2='C:\msys64\home\top1\transmission\build3\test\solverCapture.bin';

res1=readStateFile(dv1);
res2=readStateFile(dv2);

%%
for tt=1:length(res1.data)
    if (~isequal(res1.data{tt},res2.data{tt}))
        tt
    end
end

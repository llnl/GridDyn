file='C:\Users\top1\Documents\codeProjects\transmission\build\test\statefile.dat';

sf=readStateFile(file);

state=zeros(107,18);
deriv=zeros(107,18);
ss=1;
dd=1;
for pp=1:214
    if (sf.code(pp)==0)
        state(ss,:)=sf.data{pp};
        ss=ss+1;
    elseif (sf.code(pp)==1)
         deriv(dd,:)=sf.data{pp};
        dd=dd+1;
    end
end

for kk=1:18
    figure(1);
    plot(state(:,kk));
    title(sprintf('state %d',kk));
    figure(2);
    plot(deriv(:,kk));
    title(sprintf('state deriv %d',kk));
    pause;
end

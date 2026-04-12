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

%script to scan files for class information and extract some details
filedir={};
filedir{1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\core\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\primary\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\generators\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\loadModels\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\linkModels\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\sourceModels\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\submodels\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\submodels\genModels\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\submodels\governors';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\submodels\controlBlocks';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\submodels\exciters';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\submodels\pss';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\simulation\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\relays\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\controllers\';
filedir{end+1}='C:\Users\top1\Documents\codeProjects\transmission\ExtraModels\';
cls=[];
ignoreClasses={'gridObjectHolder','typeFactory','childTypeFactory','typeFactoryArg'};
for ff=1:length(filedir)
    dlist=dir(fullfile(filedir{ff},'*.h'));

    for nn=1:length(dlist)
        tempCls=getClassInfo(fullfile(filedir{ff},dlist(nn).name));
        for pp=1:length(tempCls)
            if (isempty(tempCls(pp).name))
                continue
            end
            if (ismember(tempCls(pp).name,ignoreClasses))
                continue;
            end
            if isempty(cls)
                cls=tempCls(pp);
            else
                cls(end+1)=tempCls(pp);
            end
        end

    end
end
%%
for ff=1:length(filedir)
    dlistCPP=dir(fullfile(filedir{ff},'*.cpp'));
    for nn=1:length(dlistCPP)
        tempCls=getClassSetInfo(fullfile(filedir{ff},dlistCPP(nn).name));
        for pp=1:length(tempCls)

            for qq=1:length(cls)
                if (isequal(cls(qq).name,tempCls(pp).name))
                    cls(qq).setData=tempCls(pp).dset;
                    break;
                end
            end
        end
    end
end
%%
for nn=1:length(cls)
    cls(nn).used=0;
end
%%
%order={'gridCoreObject','gridObject','gridPrimary','gridSecondary','gridSubModel'};
%saveDir='C:\Users\top1\Documents\codeProjects\transmission\docs\manuals\inputTables';
%generateLatexTables(cls,order,saveDir);

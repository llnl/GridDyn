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

function generateLatexTables(cls,order,saveDir)
% Generate the latex tables and other documentation
% generateLatexTables(cls,order,saveDir)
% cls is a structure containing a bunch of information
% order is a list of the class that should come first
% saveDir is the location to store the files

if (exist(fullfile(saveDir,'objectInputs.tex'),'file'))
    delete(fullfile(saveDir,'objectInputs.tex'));
end
for cc=1:length(order)
    ind=findObject(cls,order{cc});
    if ind>0
        str=genTable(cls,ind);
        makeLatexFile(cls(ind),str,saveDir);
        cls(ind).used=1;
    end
end
fid=fopen(fullfile(saveDir,'objectInputs.tex'),'a');
    fprintf(fid,'\\clearpage\n');
    fclose(fid);
    scnt=0;
for qq=1:length(cls)
    if (cls(qq).used==1)
        continue;
    end
    if (~isempty(cls(qq).parent))
        pind=findObject(cls,cls(qq).parent);
        if (pind>0)
            if (cls(pind).used==0)
                str=genTable(cls,pind);
                 makeLatexFile(cls(pind),str,saveDir);
                cls(pind).used=1;
                scnt=scnt+1;
            end
        end
    end
    str=genTable(cls,qq);
     makeLatexFile(cls(qq),str,saveDir);
    cls(qq).used=1;
    scnt=scnt+1;
    if (scnt>10)
       fid=fopen(fullfile(saveDir,'objectInputs.tex'),'a');
    fprintf(fid,'\\clearpage\n');
    fclose(fid);
    scnt=0;
    end
end

end

function index=findObject(cls,name)
for qq=1:length(cls)
    if isequal(cls(qq).name,name)
        index=qq;
        return
    end
end
index=0;
end


% \begin{table}[ht]
%
%       \caption{Solver Control Options} % title of Table
%       \centering % used for centering table
%       \begin{tabular}{l c p{8cm}} % centered columns (4 columns)
%           \hline %inserts double horizontal lines
%           parameter & default & description \\ [0.5ex] % inserts table
%           %heading
%           \hline % inserts single horizontal line
%           printlevel & error(1) & may be specified with a string or number "debug"(2), "error"(1), "none"(0), "error" only prints out error messages \\ % inserting body of the table
%           approx & "none" & see Table~\ref{table:approxmodes} for details on possible options \\
%           flags &  & see Table~\ref{table:solverFlags} for details on available flags \\
%           tolerance & 1e-8 & the residual tolerance to use\\
%           name & solver\_\# & the name of the solver \\
%           index & automatic & the specified index of the solver \\
%           file & & log file for the solver \\% [1ex] adds vertical space
%           \hline %inserts single line
%       \end{tabular}
%       \label{table:solverOptions}
%   \end{table}

function str=genTable(cls,qq)
tdata=cls(qq);

ccnt=1;
dblock=cell(0,5);
for vv=1:size(tdata.setData,1)
    if (tdata.setData{vv,1}(1)=='#')
        continue;
    end
    key=findVariableIndex(tdata,vv);

    if (key~=0)
        found=0;


        for kk=1:ccnt-1
            if (dblock{kk,1}==key)
                dblock{kk,2}=[dblock{kk,2},', ',nameTranslate(tdata.setData{vv,1})];
                found=1;
                break;
            end
        end
        if (found==0)
            if (key>0)
                dblock{ccnt,1}=key;
                dblock{ccnt,2}=nameTranslate(tdata.setData{vv,1});
                dblock{ccnt,3}=tdata.setData{vv,3};
                def=tdata.variables{key,2};
                if (isnumeric(def))
                    dblock{ccnt,4}=num2str(def);
                else
                    dblock{ccnt,4}=nameTranslate(def);
                end
                if (~isempty(tdata.variables{key,3}))
                    dblock{ccnt,5}=nameTranslate(strtrim(tdata.variables{key,3}));

                end
                ccnt=ccnt+1;
            else
                sname=tdata.setData{-key,1};
                for pp=1:size(dblock,1)
                    if (isequal(sname,dblock{pp,2}))
                        dblock{pp,1}=key;
                        dblock{pp,2}=[dblock{pp,2},', ',nameTranslate(tdata.setData{vv,1})];
                    end
                end


            end

        end
    else
        dblock{ccnt,2}=nameTranslate(tdata.setData{vv,1});
        dblock{ccnt,3}=tdata.setData{vv,3};
        [def,desc]=lookupParentVariable(cls,qq,vv);
                 if (isnumeric(def))
                    dblock{ccnt,4}=num2str(def);
                else
                    dblock{ccnt,4}=nameTranslate(def);
                 end
                 if (~isempty(desc))
                    dblock{ccnt,5}=nameTranslate(strtrim(desc));
                 end
        ccnt=ccnt+1;
    end

end
bigTable=(size(dblock,1)>20);

if (bigTable)
str=sprintf('\\begin{longtable}{p{5cm} c c p{7cm}}\n') ;
else
    str=sprintf('\\begin{table}[ht]\n');
end


if (~bigTable)
    str=[str,sprintf('\\centering\n')];
    str=[str,sprintf('\\begin{tabular}{p{5cm} c c p{7cm}}\n\\hline\n')];
end
str=[str,sprintf('string(s) & type & default & description \\\\\n')];
str=[str,sprintf('\\hline\n')];

for mm=1:size(dblock,1)
    str=[str,sprintf('%s & %s & %s & %s\\\\\n',dblock{mm,2},dblock{mm,3},dblock{mm,4},dblock{mm,5})];
end
str=[str,sprintf('\\hline\n')];
if (~bigTable)
    str=[str,sprintf('\\end{tabular}\n')];
end
if (~isempty(tdata.parent))
    str=[str,sprintf('\\caption{Set options for %s objects. See Table~\\ref{table:%s} for additional settable options through %s}\n',tdata.name,tdata.parent,tdata.parent)];
else
    str=[str,sprintf('\\caption{Set options for %s objects.}\n',tdata.name)];
end
str=[str,sprintf('\\label{table:%s}\n',tdata.name)];

if (bigTable)
str=[str,sprintf('\\end{longtable}\n')];
else
   str=[str,sprintf('\\end{table}\n')];
end

end

function nstr=nameTranslate(str)
nstr = strrep(str,'_','\_');
nstr = strrep(nstr,'%','\%');
nstr = strrep(nstr,'^','\^');
end

function key=findVariableIndex(tdata,vv)
key=0;
if (~isempty(tdata.setData{vv,2}))
    for nn=1:size(tdata.variables,1)
        if (isequal(tdata.setData{vv,2},tdata.variables{nn,1}))
            key=nn;
            break;
        end
    end
    if (key==0)
        for nn=1:vv-1
            if (isequal(tdata.setData{vv,2},tdata.setData{nn,2}))
                key=-nn;
                break;
            end
        end
    end
else
    str=tdata.setData{vv,1};
     for nn=1:size(tdata.variables,1)
        if (isequal(str,lower(tdata.variables{nn,1})))
            key=nn;
            break;
        end
    end
end
end




function [def,desc]=lookupParentVariable(cls,qq,key)
tdata=cls(qq);
vname=tdata.setData{key,2};
found=0;
def=0;
desc='';
index=qq;
while (~found)
    index=findObject(cls,cls(index).parent);
    if (index>0)
        key=findVariable(cls(index),vname);
        if (key>0)
            def=cls(index).variables{key,2};
            desc=cls(index).variables{key,3};
            found=1;
        end
    else
        break;
    end
end
end

function key=findVariable(tdata,vname)
key=0;
    for nn=1:size(tdata.variables,1)
        if (isequal(vname,tdata.variables{nn,1}))
            key=nn;
            break;
        end
    end
    if (key==0)
        for nn=1:size(tdata.setData,1)
            if (isequal(vname,tdata.setData{nn,2}))
                key=-nn;
                break;
            end
        end
    end

end

function makeLatexFile(tdata,str,saveDir)

fid=fopen(fullfile(saveDir,sprintf('%sInputTable.tex',tdata.name)),'w');
fprintf(fid,'%s\n',str);
fclose(fid);
if (exist(fullfile(saveDir,'objectInputs.tex'),'file'))
    fid=fopen(fullfile(saveDir,'objectInputs.tex'),'a');
    fprintf(fid,'\\input{%sInputTable.tex}\n',tdata.name);
    fclose(fid);
else
    fid=fopen(fullfile(saveDir,'objectInputs.tex'),'w');
    fprintf(fid,'\\input{%sInputTable.tex}\n',tdata.name);
    fclose(fid);
end
end

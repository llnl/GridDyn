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

function [num,txt,adata,total_rows]=sheetread(varargin)
%function to read any sort of spreadsheet that it can
%num is a numeric array of all numbers, txt is a cell array of all the
%things that didn't go in the num array, and adata is a cell array of all
%the data in the cells
% [num,txt,adata,total_rows]=sheetread(filename,<param>, <value>)
% 'type' specify the type of the file as one of
% 'xls','xlsx','csv','tsv','auto','','custom' otherwise interpreted from the
% file extension
% 'date_cols' can specify that specific columns should be written as a date
% 'date_format' can specify the format for the dates (see mat datenum for
% details
% 'rows'  read N rows or rows from [M,N]
%  'cols' read N cols or cols from [M,N]
% 'delimiter',  the delimiter for a delimited file

valid_types={'xls','xlsx','csv','tsv','auto','','custom'};
p=inputParser();
p.addRequired('fname',@(x) exist(x,'file'));
p.addParamValue('type','',@(x) isempty(x)||ismember(lower(x),valid_types));
p.addParamValue('rows',[1,inf],@(x) (numel(x)<=3));
p.addParamValue('cols',[1,inf],@(x) (numel(x)<=3));
p.addParamValue('date_cols',[],@isnumeric);
p.addParamValue('date_format','');
p.addParamValue('delimiter',',',@(x) ischar(x));


p.StructExpand=true;
p.parse(varargin{:});

params=p.Results;

if (isempty(params.type))
[~,~,ext]=fileparts(params.fname);
params.type=ext(2:end);
end

persistent startrow;
if isempty(startrow)
    startrow=1;
end

if (numel(params.rows)<3)
    switch(numel(params.rows))
        case 2
            params.rows(3)=1;
        case 1
            params.rows(2)=params.rows(1);
            params.rows(3)=1;
            params.rows(1)=startrow;
    end
end
startrow=startrow+params.rows(2)*params.rows(3);
if (numel(params.cols)<3)
    switch(numel(params.cols))
        case 2
            params.cols(3)=1;
        case 1
            params.cols(2)=inf;
            params.cols(3)=1;
    end
end

total_rows=[];
switch (params.type)
    case {'xls','xlsx'}
        [num,txt,adata]=xlsread(params.fname);
    case {'csv'}
        [num,txt,adata,total_rows]=txtsheetread(params.fname,',',params);
    case {'tsv'}
        [num,txt,adata,total_rows]=txtsheetread(params.fname,'\t',params);
    case {'custom'}
        [num,txt,adata,total_rows]=txtsheetread(params.fname,params.delimiter,params);
    otherwise
        if (ispc)
            [num,txt,adata]=xlsread(params.fname);
        else
            num=[];
            txt={};
            adata={};
            fprintf('unrecognized file type');
        end
end

end


function [num,txt,adata,total_rows]=txtsheetread(fname,separator,params)

fid=fopen(fname,'r');

persistent pfname;
persistent lindex;
persistent lupdate;
persistent lcnt;
persistent maxl;
persistent usecplx;
finfo=dir(fname);
regen=0;
if (~isequal(pfname,fname))
    pfname=fname;
    regen=1;
end

if (isempty(lupdate))||(finfo.datenum~=lupdate)
    regen=1;

end

if (regen)
    lcnt=0;
    maxl=0;
    usecplx=0;
    while 1
        if (lcnt<=100)
            tline = fgets(fid);
            if ~ischar(tline)
                lindex=[1,1];
                break
            end
            lcnt=lcnt+1;
            if (lcnt==101)
                cind=ftell(fid);
            end
            if (any(tline=='"'))
                sp={};
                cpos=1;
                spcount=0;
                while cpos<=length(tline)
                    if (tline(cpos)=='"')
                        m=find((tline(cpos+1:end)=='"'),1,'first');
                        spcount=spcount+1;
                        if(m==1)
                            sp{spcount}='';

                        else
                            sp{spcount}=tline(cpos+1:cpos+1+m-2);
                        end
                        cpos=cpos+m+1;
                        m=find((tline(cpos:end)==separator),1,'first');
                        cpos=cpos+m;
                    else
                        m=find((tline(cpos:end)==separator),1,'first');
                        spcount=spcount+1;
                        if (m==1)
                            sp{spcount}='';
                            cpos=cpos+1;
                        else
                            sp{spcount}=tline(cpos:cpos+m-2);
                            cpos=cpos+m;
                        end
                    end
                end
                sp{end}=strtrim(sp{end});
            else
                sp=regexp(tline,separator,'split');
                sp{end}=strtrim(sp{end});
            end
            if (length(sp)>maxl)
                maxl=length(sp);

            end
            vals=cellfun(@str2double,sp);
            if (~all(isreal(vals)))
                usecplx=1;
            end
            vals2=cellfun(@fsstr2double,sp);
            if (sum(isnan(vals))~=sum(isnan(vals2)))
                usecplx=1;
            end
        elseif (lcnt<=200)
            tline = fgets(fid);
            if ~ischar(tline)
                lindex=[1,1];
                break
            end
            lcnt=lcnt+1;
            if (lcnt==201)
                sind=ftell(fid);
                lsize=(sind-cind)/100;
                numlines=((finfo.bytes-cind)/lsize)*1.1+100;
                lindex=ones(ceil(numlines/1000),2);
                n1000=1000;
            end
        else
            [dt,dtnum]=fread(fid,16192,'char=>char');
            if (dtnum<16192)
                 s=regexp(dt','\n','start');
                lcnt=lcnt+length(s);
                while (lcnt>n1000)
                    ind=s(end-(lcnt-n1000)+1);
                    n1000=n1000+1000;
                    lindex(n1000/1000+1,1)=n1000;
                    lindex(n1000/1000+1,2)=ftell(fid)-dtnum+ind+1;
                end
                break;
            else
                 s=regexp(dt','\n','start');
                lcnt=lcnt+length(s);
                while (lcnt>n1000)
                    ind=s(end-(lcnt-n1000)+1);

                    lindex(n1000/1000+1,1)=n1000;
                    lindex(n1000/1000+1,2)=ftell(fid)-16192+ind+1;
                    n1000=n1000+1000;
                end

            end
        end
    end
    lupdate=finfo.datenum;
    fseek(fid,0,'bof');
end

colnum=min(ceil((params.cols(2))/params.cols(3)),ceil((maxl-params.cols(1)+1)/params.cols(3)));
vcols=zeros(colnum,1);
vcols(params.cols(1):params.cols(3):params.cols(1)+min((params.cols(2)-1)*params.cols(3),maxl-params.cols(1)))=1;
rownum=min(ceil((params.rows(2))/params.rows(3)),ceil((lcnt-params.rows(1)+1)/params.rows(3)));
num=nan(rownum,colnum);
txt=cell(rownum,colnum);
adata=cell(rownum,colnum);

if (params.rows(1)>1000)
    lookupindex=floor(params.rows(1)/1000)+1;
    fseek(fid,lindex(lookupindex,2),'bof');
    crcnt=lindex(lookupindex,1)+1;
else
    crcnt=0;
end
indcnt=0;
skcnt=inf;
while 1

    tline = fgets(fid);
    crcnt=crcnt+1;
    if ~ischar(tline), break, end

    if (crcnt<params.rows(1))

        continue;
    elseif (indcnt>params.rows(2))
        break;
    elseif (skcnt<params.rows(3))

        skcnt=skcnt+1;
        continue;
    else
        skcnt=1;
        indcnt=indcnt+1;

    end

    if (any(tline=='"'))

        m=find(tline=='"');
        nn=1;
        kk=0;
        gv={};
        while (nn<=length(m)-1)
            kk=kk+1;
            gv{kk}=tline(m(nn)+1:m(nn+1)-1);
            tline(m(nn):m(nn+1))='#';
            nn=nn+2;
        end
        sp=regexp(tline(1:end-1),separator,'split');
        sp{end}=strtrim(sp{end});
        kk=0;
        for nn=1:length(sp)
            if (isempty(sp{nn}))
                continue;
            end
            sp{nn}=strtrim(sp{nn});
            if (sp{nn}(1)=='#')
                kk=kk+1;
                sp{nn}=gv{kk};
            end
        end
%         while cpos<=length(tline)
%             if (tline(cpos)=='"')
%                  m=find((tline(cpos+1:end)=='"'),1,'first');
%                  spcount=spcount+1;
%                  if(m==1)
%                      sp{spcount}='';
%
%                  else
%                      sp{spcount}=tline(cpos+1:cpos+1+m-2);
%                  end
%                  cpos=cpos+m+1;
%                  m=find((tline(cpos:end)==separator),1,'first');
%                  cpos=cpos+m;
%             else
%                 m=find((tline(cpos:end)==separator),1,'first');
%                 spcount=spcount+1;
%                 if (m==1)
%                     sp{spcount}='';
%                     cpos=cpos+1;
%                 else
%                     sp{spcount}=tline(cpos:cpos+m-2);
%                     cpos=cpos+m;
%                 end
%             end
%         end
%         sp{end}=strtrim(sp{end});
    else
        sp=regexp(tline(1:end-1),separator,'split');
        sp{end}=strtrim(sp{end});
    end
    colind=0;
    if (usecplx)
        vals=cellfun(@str2double,sp);
    else
        vals=cellfun(@fsstr2double,sp);
    end
    %just in case we have an odd row we didn't catch earlier
    if (length(sp)>colnum)
        for mm=length(vcols)+1:length(sp)
            vcols(mm)=1;
            num(:,mm)=nan;

        end
        colnum=min(ceil((params.cols(2))/params.cols(3)),ceil((length(sp)-params.cols(1)+1)/params.cols(3)));
        vcols=zeros(colnum,1);
        vcols(params.cols(1):params.cols(3):params.cols(1)+min((params.cols(2)-1)*params.cols(3),length(sp)-params.cols(1)))=1;

    end
    for kk=1:length(sp)
        if (vcols(kk)==1)
            colind=colind+1;
            num(indcnt,colind)=vals(kk);
            if isnan(num(indcnt,colind))
                txt{indcnt,colind}=strtrim(sp{kk});
                adata{indcnt,colind}=txt{indcnt,colind};
            else
                adata{indcnt,colind}=num(indcnt,colind);
            end
        end
    end

end
for cc=1:size(adata,2)
    if ismember(cc,params.date_cols)
        try
            try
                if (~isempty(params.date_format))
                dt1=datenum(adata{1,cc},params.date_format);
                else
                    dt1=datenum(adata{1,cc});
                end
                num(1,cc)=dt1;
                adata{1,cc}=dt1;
            end
             if (~isempty(params.date_format))
            dt=datenum(adata(2:end,cc),params.date_format);
             else
                 dt=datenum(adata(2:end,cc));
             end
            num(2:end,cc)=dt;
            adata(2:end)=num2cell(dt);
        catch
            for rr=2:size(adata,1)
                try
                    if (~isempty(params.date_format))
                    dt1=datenum(adata{rr,cc},params.date_format);
                    else
                        dt1=datenum(adata{rr,cc});
                    end
                    num(rr,cc)=dt1;
                    adata{rr,cc}=dt1;
                end
            end
        end
    end
end
fclose(fid);
total_rows=lcnt;
end


function x=fsstr2double(s)

[a,count,errmsg,nextindex] = sscanf(s,'%f',1);
    if count == 1 && isempty(errmsg) && nextindex > numel(s)
        x = a;
    else
        x=nan;
    end
end

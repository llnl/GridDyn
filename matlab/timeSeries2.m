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
classdef timeSeries2 < handle
    %class to operate on a time series2 (and 1) data as generated from GridDyn

    properties

        description='';
        time=[];
        data=[];
        cols=1;
        count=0;
        fields={''};
    end

    methods
        function ts=timeSeries2(varargin)
            if (nargin==1)
                if (ischar(varargin{1}))
                     [~,~,ext]=fileparts(varargin{1});
                     if (isequal(ext,'.csv'))
                         loadTextFile(ts,varargin{1});
                     else
                         loadBinaryFile(ts,varargin{1});
                     end
                end
            elseif(nargin==2)
                if isnumeric(varargin{1})
                    ts.time=varargin{1};
                    ts.count=length(ts.time);
                end
                if isnumeric(varargin{2})

                    [r,c]=size(varargin{2});
                    if (r==ts.count)
                        ts.data=varargin{2};
                    else
                       ts.data=varargin{2}';
                    end
                    ts.cols=size(ts.data,2);
                end
            end
        end

        function addData(ts,t,val,colnum)
            if (nargin<4)
                colnum=1;
            end
            if (numel(val)==ts.cols)
                ts.time(ts.count+1)=t;
                ts.data(ts.count+1,:)=val;
                ts.count=ts.count+1;
            elseif (numel(val)==1)
                ts.time(ts.count+1)=t;
                ts.data(ts.count+1,colnum)=val;
                ts.count=ts.count+1;
            elseif (length(t)==size(val,1))
                ep=length(t);
                ts.time(ts.count+1:ts.count+ep,1)=t;
                ts.data(ts.count+1:ts.count+ep,colnum:colnum+size(val,2)-1)=val;
                ts.count=ts.count+ep;
                setCols(ts,size(ts.data,2));
            elseif (length(t)==size(val,2))
                ep=length(t);

                ts.time(ts.count+1:ts.count+ep,1)=t;
                ts.data(ts.count+1:ts.count+ep,colnum:colnum+size(val,1)-1)=val';
                ts.count=ts.count+ep;
                setCols(ts,size(ts.data,2));
            else
                error('invalid data size');
            end
        end

        function addColumn(ts,val,colnum)
             if (nargin<3)
                colnum=1;
            end
            if (size(val,1)==ts.count)
                ts.data(:,colnum:colnum+size(val,2)-1)=val;
                setCols(ts,size(ts.data,2))

            elseif (size(val,2)==ts.count)
               ts.data(:,colnum:colnum+size(val,1)-1)=val';
               setCols(ts,size(ts.data,2))
            else
                error('invalid data size');
            end
        end

        function setSize(ts,newSize)
            ts.count=newSize;
           if (newSize>length(ts.time))
                ts.time(end:newSize)=NaN;
                ts.data(end:newSize,:)=NaN;
            end
        end
        function setCapacity(ts,newSize)
            if (newSize>length(ts.time))
                ts.time(end:newSize)=NaN;
                ts.data(end:newSize,:)=NaN;
            end
        end

        function setCols(ts,newCols)
            if (newCols>ts.cols)

                ts.data(:,end+1:newCols)=NaN;
                [ts.fields{end+1:newCols}]=deal('');
            end
            ts.cols=newCols;
        end

        function loadBinaryFile(ts,filename)

            if (~exist(filename,'file'))
                return;
            end

            fid=fopen(filename,'rb');
            if (fid<0)

                return;
            end
            dflag=fread(fid,1,'int32');
            if (dflag~=1)
                fclose(fid);
                fid=fopen(filename,'rb','ieee-be');
                dflag=fread(fid,1,'int32');
                if (dflag~=1)
                    fclose(fid);
                    return;
                end
            end

            dcount=fread(fid,1,'int32');
            if (dcount>0)
                desc=fread(fid,dcount,'char=>char')';
            else
                desc='';
            end
            ts.description=desc;
            % make sure it looks like a string
            if (size(ts.description,1)>1)
                ts.description=ts.description';
            end
            nc=fread(fid,1,'int32');
            rc=fread(fid,1,'int32');
            for ii=1:rc-1
                flen=fread(fid,1,'uint8');
                if (flen>0)
                    ts.fields{ii}=fread(fid,flen,'char=>char')';
                else
                    ts.fields{ii}=sprintf('field_%d',ii);
                end
            end
            ts.count=nc;
            ts.cols=rc-1;
            if (rc>=1)
                ts.time=fread(fid,nc,'double');
            end
            if (rc>1)
                ts.data=zeros(nc,rc-1);
                for kk=1:rc-1
                    try
                        ts.data(:,kk)=fread(fid,nc,'double');
                    catch e
                        break;
                    end
                end
            end

            fclose(fid);
        end

        function loadTextFile(ts,filename)
            if (~exist(filename,'file'))
                return;
            end
            [nm,~,at]=sheetread(filename);

            ts.description=at{1,1};
            ts.count=size(at,1)-2;
            ts.cols=size(at,2)-1;
            ts.time=nm(2:end,1);
            ts.data=nm(2:end,2:end);
            ts.fields=at(2,2:end);

        end

        function writeBinaryFile(ts,filename)
            [pth,name,ext]=fileparts(filename);
            if isempty(pth)
                fname=fullfile(pwd,filename);
            else
                fname=filename;
            end

            fid=fopen(fname,'wb');
            if (fid<0)
                error('unable to open file');
            end
            fwrite(fid,int32(1),'int32');
            if (~isempty(ts.description))
                fwrite(fid,int32(length(ts.description)),'int32');
                fwrite(fid,ts.description,'char');
            else
                fwrite(fid,int32(0),'int32');
            end
            rc=ts.cols+1;
            nc=ts.count;
            fwrite(fid,int32(nc),'int32');
            fwrite(fid,int32(rc),'int32');
             for ii=1:rc-1
                flen=length(ts.fields{ii});
                if (flen>0)
                    fwrite(fid,uint8(flen),'uint8');
                    fwrite(fid,ts.fields{ii},'char');
                else
                    fwrite(fid,uint8(0),'uint8');
                end
            end

             fwrite(fid,ts.time,'double');
            for kk=1:ts.cols
                fwrite(fid,ts.data(:,kk),'double');
            end
            fclose(fid);
        end
        function writeTextFile(ts,filename)
        end

        function varargout=subsref(ts,s)
            switch (s(1).type)
                case '.'
                    if (nargout==0)
                        if (ischar(s(1).subs))
                            if (ismember(s(1).subs,methods(timeSeries2)))
                                fh = str2func(s(1).subs);
                                if (iscell(s(2).subs))
                                    fh(ts,s(2).subs{:});
                                else
                                    fh(ts,s(2).subs);
                                end
                            else
                                varargout = {builtin('subsref',ts,s)};
                            end
                        else
                             varargout = {builtin('subsref',ts,s)};
                        end
                    else
                        varargout = {builtin('subsref',ts,s)};
                    end
                case '()'
                    if (length(s)==1)
                        if (length(s(1).subs)==1)
                            varargout{1}=ts.data(:,s(1).subs{1});
                        elseif (length(s(1).subs)==2)
                            varargout = {builtin('subsref',ts.data,s)};
                        else
                            error('invalid indices');
                        end
                    else
                        varargout = {builtin('subsref',ts.data,s)};
                    end
                case '{}'
                    varargout = {builtin('subsref',ts,s)};
            end
        end
    end
end

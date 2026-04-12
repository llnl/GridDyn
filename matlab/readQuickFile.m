function ts=readQuickFile(filename)
%function reads a quick binary file with double precision for the data

[pth,name,ext]=fileparts(filename);
if (~exist(filename,'file'))
    ts=[];
    return;
end

fid=fopen(filename,'rb');
if (fid<0)
    ts=[];
    return;
end
dflag=fread(fid,1,'int32');
if (dflag~=1)
    fclose(fid);
    fid=fopen(filename,'rb','ieee-be');
    dflag=fread(fid,1,'int32');
    if (dflag~=1)
        fclose(fid);
        ts=[];
        return;
    end
end

dcount=fread(fid,1,'int32');
if (dcount>0)
    desc=fread(fid,dcount,'char=>char');
else
    desc='';
end
ts.description=desc';
nc=fread(fid,1,'int32');
rc=fread(fid,1,'int32');
for ii=1:rc-1
    flen=fread(fid,1,'uint8');
    if (flen>0)
        ts.fields{ii}=fread(fid,flen,'char=>char');
    else
        ts.fields{ii}=sprintf('field_%d',ii);
    end
end

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

function out=writeQuickFile(filename,varargin)
%function creates a quick binary file with double precision for the data

[pth,name,ext]=fileparts(filename);
if isempty(pth)
    fname=fullfile(pwd,filename);
else
    fname=filename;
end

fid=fopen(fname,'wb');
if (fid<0)
    out=-1;
    return;
end
if (ischar(varargin{1}))
    fwrite(fid,int32(length(varargin{1})),'int32');
    fwrite(fid,varargin{1},'char');
    vs=2;
else
    fwrite(fid,int32(0),'int32');
    vs=1;
end
nc=length(varargin{vs});
rc=nargin-vs;
fwrite(fid,int32(nc),'int32');
fwrite(fid,int32(rc),'int32');
for kk=vs:nargin-1
   fwrite(fid,varargin{kk},'double');
end
fclose(fid);
out=0;

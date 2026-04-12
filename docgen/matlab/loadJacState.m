function [state,Jac]=loadJacState(fname)
%function to load the jacobian and state from gridDyn
fid=fopen(fname);
if (fid<=0)
    error('Unable to open file\n');
end
%read the state size and state
stateSize=fread(fid,1,'uint32');
state=fread(fid,stateSize,'double');

%read the jacobian

Jac=fread(fid,stateSize*stateSize,'double');
%reshape the jacobian
Jac=reshape(Jac,[stateSize,stateSize]);

Jac=Jac';

fclose(fid);

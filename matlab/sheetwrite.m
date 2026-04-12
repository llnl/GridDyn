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

function sheetwrite(fname,vals,varargin)
%sheetwrite(fname,vals,varargin)
%function to write any sort of spreadsheet that it can
%vals is a numeric array or cell array
% optional arguments are type('auto') or one of 'xls','xlsx','csv','tsv','auto', precision
% (7),delimiter(',')
% vals=rand(10,10);
% sheetwrite('randArray.txt',vals,'delimiter',';','precision',4);  to
% create a file with a 10x10 random number array separated by semicolons written with a precision
% of 4 digits
% 'date_cols' can specify that specific columns should be written as a date
% 'date_format' can specify the format for the dates (see mat datenum for
% details
% header can be given as a cell array for the first row of data if val is
% numeric
% startrow can specify to start writing the data on row startrow of val

valid_types={'xls','xlsx','csv','tsv','auto','','custom'};
p=inputParser();
p.addParamValue('type','',@(x) isempty(x)||ismember(lower(x),valid_types));
p.addParamValue('precision',7, @isnumeric);
p.addParamValue('delimiter',',', @(x) ischar(x) && length(x)==1);
p.addParamValue('date_cols',[],@isnumeric);
p.addParamValue('date_format',0);
p.addParamValue('header',{},@iscell);
p.addParamValue('startrow',1,@isnumeric);
p.StructExpand=true;
p.parse(varargin{:});

params=p.Results;

if (isempty(params.type))||(isequal(params.type,'auto'))
[~,~,ext]=fileparts(fname);
params.type=ext(2:end);
end

switch (params.type)
    case {'custom'}
        txtsheetwrite(fname,vals,params.delimiter,params);
    case {'xls','xlsx'}
        xlswrite(params.fname,vals);
    case {'csv'}
        txtsheetwrite(fname,vals,',',params);
    case {'tsv'}
        txtsheetwrite(fname,vals,'\t',params);
    otherwise
        if (ispc)
            xlswrite(fname,vals);
        else

            fprintf('unrecognized file type');
        end
end

end

function txtsheetwrite(fname,vals,delim,params)

fid=fopen(fname,'w');
if (fid<0)
     fprintf(2,'Unable to open file for writing\n');
     return;
end

spec=sprintf('%%.%df',params.precision);
if (iscell(vals))
    for rr=1:size(vals,1)

        for cc=1:size(vals,2)
            if isnumeric(vals{rr,cc})
                if (isnan(vals{rr,cc}))
                    d='';
                elseif (ismember(cc,params.date_cols))
                    d=datestr( vals{rr,cc},params.date_format);
                elseif (round(vals{rr,cc})==vals{rr,cc})
                    d=sprintf('%d',vals{rr,cc});
                else
                    d=sprintf(spec,vals{rr,cc});
                end
            else
                d=vals{rr,cc};
                if (any(d==delim))
                    d=['"',d,'"'];
                end
            end
            if (cc==1)
                fprintf(fid,'%s',d);

            else
                fprintf(fid,'%s%s',delim,d);
            end
        end
        fprintf(fid,'\n');
    end
elseif (isnumeric(vals))
    if (~isempty(params.header))
        for rr=1:size(params.header,1)

        for cc=1:size(params.header,2)
            if isnumeric(params.header{rr,cc})
                if (isnan(params.header{rr,cc}))
                    d='';
                elseif (round(params.header{rr,cc})==params.header{rr,cc})
                    d=sprintf('%d',params.header{rr,cc});
                else
                    d=sprintf(spec,params.header{rr,cc});
                end
            else
                d=params.header{rr,cc};
                if (any(d==delim))
                    d=['"',d,'"'];
                end
            end
            if (cc==1)
                fprintf(fid,'%s',d);

            else
                fprintf(fid,'%s%s',delim,d);
            end
        end
        fprintf(fid,'\n');
        end
    end
    for rr=params.startrow:size(vals,1)
        if (isnan(vals(rr,1)))
        elseif (ismember(1,params.date_cols))
             fprintf(fid,'%s',datestr( vals(rr,1),params.date_format));
        elseif (round(vals(rr,1))==vals(rr,1))
            fprintf(fid,'%d',vals(rr,1));
        else
            fprintf(fid,spec,vals(rr,1));
        end
        for cc=2:size(vals,2)
            fprintf(fid,'%s',delim);
            if (isnan(vals(rr,cc)))
            elseif (ismember(cc,params.date_cols))
             fprintf(fid,'%s',datestr( vals(rr,cc),params.date_format));
            elseif (round(vals(rr,cc))==vals(rr,cc))
                fprintf(fid,'%d',vals(rr,cc));
            else
                fprintf(fid,spec,vals(rr,cc));
            end
        end
        fprintf(fid,'\n');
    end
end
fclose(fid);
end

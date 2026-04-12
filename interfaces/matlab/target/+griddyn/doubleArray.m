classdef doubleArray < SwigRef
  methods
    function this = swig_this(self)
      this = griddynMEX(3, self);
    end
    function self = doubleArray(varargin)
      if nargin==1 && strcmp(class(varargin{1}),'SwigRef')
        if ~isnull(varargin{1})
          self.swigPtr = varargin{1}.swigPtr;
        end
      else
        tmp = griddynMEX(15, varargin{:});
        self.swigPtr = tmp.swigPtr;
        tmp.swigPtr = [];
      end
    end
    function delete(self)
      if self.swigPtr
        griddynMEX(16, self);
        self.swigPtr=[];
      end
    end
    function varargout = paren(self,varargin)
      [varargout{1:nargout}] = griddynMEX(17, self, varargin{:});
    end
    function varargout = paren_asgn(self,varargin)
      [varargout{1:nargout}] = griddynMEX(18, self, varargin{:});
    end
    function varargout = cast(self,varargin)
      [varargout{1:nargout}] = griddynMEX(19, self, varargin{:});
    end
  end
  methods(Static)
    function varargout = frompointer(varargin)
     [varargout{1:nargout}] = griddynMEX(20, varargin{:});
    end
  end
end

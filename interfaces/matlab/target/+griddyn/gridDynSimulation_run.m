function varargout = gridDynSimulation_run(varargin)
  [varargout{1:nargout}] = griddynMEX(50, varargin{:});
end

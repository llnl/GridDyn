function varargout = gridDynSimulation_initializeFromArgs(varargin)
  [varargout{1:nargout}] = griddynMEX(42, varargin{:});
end

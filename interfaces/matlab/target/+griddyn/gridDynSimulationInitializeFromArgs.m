function varargout = gridDynSimulationInitializeFromArgs(varargin)
  [varargout{1:nargout}] = griddynMEX(42, varargin{:});
end

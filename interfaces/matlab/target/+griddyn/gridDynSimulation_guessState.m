function varargout = gridDynSimulation_guessState(varargin)
  [varargout{1:nargout}] = griddynMEX(64, varargin{:});
end

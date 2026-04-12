function v = griddyn_solve_error()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 8);
  end
  v = vInitialized;
end

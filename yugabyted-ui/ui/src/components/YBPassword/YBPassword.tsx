import React, { FC, useState } from 'react';
import { InputAdornment, IconButton } from '@material-ui/core';
import VisibilityIcon from '@app/assets/eye.svg';
import VisibilityOffIcon from '@app/assets/eye-slash.svg';
import { YBInput, YBInputProps } from '@app/components/YBInput/YBInput';

export const YBPassword: FC<YBInputProps & { hidePasswordButton?: boolean }> = (props) => {
  const [showPassword, setShowPassword] = useState(false);

  const handleClickShowPassword = () => {
    setShowPassword(!showPassword);
  };
  let inputProps;
  if (props.hidePasswordButton) {
    inputProps = {};
  } else {
    inputProps = {
      endAdornment: (
        <InputAdornment position="end">
          <IconButton onClick={handleClickShowPassword} tabIndex="-1">
            {showPassword ? <VisibilityIcon /> : <VisibilityOffIcon />}
          </IconButton>
        </InputAdornment>
      )
    };
  }

  return (
    <YBInput
      type={showPassword ? 'text' : 'password'}
      {...props}
      helperText={props.error ? props.helperText : ''}
      InputProps={inputProps}
    />
  );
};
